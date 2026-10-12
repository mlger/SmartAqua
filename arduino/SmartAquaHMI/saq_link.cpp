/**
 * saq_link.cpp — HMI-side ESP-NOW engine. See saq_link.h.
 */

#include "saq_link.h"
#include "app_model.h"
#include "saq_keys.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

#define TXQ_DEPTH   12
#define EVTQ_DEPTH  24

typedef struct {
    uint8_t  buf[SAQ_FRAME_MAX];
    uint16_t len;
    uint32_t seq;
    uint8_t  type;
    uint8_t  attempts;
    uint32_t next_try_ms;
} tx_cmd_t;

static tx_cmd_t       s_txq[TXQ_DEPTH];
static uint8_t        s_txq_head = 0, s_txq_count = 0;

static QueueHandle_t  s_rxq = NULL;      /* raw frames from the WiFi task */
static QueueHandle_t  s_evtq = NULL;     /* events for the UI loop */

static bool           s_peer_added = false;
static uint8_t        s_peer_mac[6] = {0};
static uint32_t       s_tx_seq = 0;
static saq_dedupe_t   s_dedupe;          /* uplink dedupe (STATUS retries etc.) */

static bool           s_pair_armed = false;
static uint32_t       s_pair_armed_ms = 0;
static bool           s_link_up = false;
static uint32_t       s_last_ack_ms = 0;

static uint32_t s_tx_frames = 0, s_tx_retries = 0;
static uint32_t s_rx_frames = 0, s_rx_dups = 0, s_rx_foreign = 0;

typedef struct {
    saq_frame_t f;
    uint8_t from[6];
} rx_item_t;

/* ------------------------------------------------------------------ */
/* Peers                                                               */
/* ------------------------------------------------------------------ */

static void peer_add(bool encrypted)
{
    if (s_peer_added) {
        esp_now_del_peer(s_peer_mac);
        s_peer_added = false;
    }
    esp_now_peer_info_t p;
    memset(&p, 0, sizeof(p));
    memcpy(p.peer_addr, s_peer_mac, 6);
    p.channel = SAQ_ESPNOW_CHANNEL;
    p.ifidx = WIFI_IF_STA;
    p.encrypt = encrypted;
    if (encrypted) {
        memcpy(p.lmk, SAQ_LMK_BYTES, 16);
    }
    s_peer_added = (esp_now_add_peer(&p) == ESP_OK);
}

/* ------------------------------------------------------------------ */
/* Sending                                                             */
/* ------------------------------------------------------------------ */

static bool tx_push(const saq_frame_t *f)
{
    if (s_txq_count >= TXQ_DEPTH) {
        return false;
    }
    uint8_t slot = (uint8_t)((s_txq_head + s_txq_count) % TXQ_DEPTH);
    const int n = saq_encode(s_txq[slot].buf, sizeof(s_txq[slot].buf), f);
    if (n <= 0) {
        return false;
    }
    s_txq[slot].len = (uint16_t)n;
    s_txq[slot].seq = f->seq;
    s_txq[slot].type = f->type;
    s_txq[slot].attempts = 0;
    s_txq[slot].next_try_ms = 0;      /* send on the next tick */
    s_txq_count++;
    return true;
}

static void evt_push(const hmi_link_evt_t *e)
{
    if (s_evtq != NULL) {
        xQueueSend(s_evtq, e, 0);
    }
}

/* Build-and-queue in one step: header + payload, then encode. The
 * per-call payload fields must be set BEFORE tx_push encodes the frame. */
static bool cmd_begin(saq_frame_t *f, uint8_t type)
{
    if (!g_app.paired) {
        return false;                 /* nothing to talk to yet */
    }
    memset(f, 0, sizeof(*f));
    f->type = type;
    f->flags = SAQ_FLAG_ACK_REQUIRED;
    s_tx_seq++;
    f->seq = s_tx_seq;
    f->epoch_s = model_epoch();     /* 0 when unsynced — informative */
    return true;
}

/* ------------------------------------------------------------------ */
/* Receive callback — WiFi task context                                */
/* ------------------------------------------------------------------ */

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if ((info == NULL) || (data == NULL) || (len <= 0)) {
        return;
    }
    const bool from_peer = (g_app.paired && (memcmp(info->src_addr, g_app.peer_mac, 6) == 0));

    /* Peer filter. The one exception: BEACON broadcasts, accepted only
     * while pairing is armed (docs/07: pairing needs a human at the tank). */
    if (!from_peer) {
        if (!s_pair_armed) {
            s_rx_foreign++;
            return;
        }
        uint8_t peek_type = 0;
        if (len >= 3) {
            peek_type = data[2];
        }
        if (peek_type != SAQ_T_BEACON) {
            s_rx_foreign++;
            return;
        }
    }

    saq_frame_t f;
    if (saq_decode(data, (size_t)len, &f) != SAQ_CODEC_OK) {
        return;
    }
    s_rx_frames++;

    rx_item_t it;
    it.f = f;
    memcpy(it.from, info->src_addr, 6);
    if (xQueueSend(s_rxq, &it, 0) != pdTRUE) {
        /* queue full — the feeder's own retry covers command frames;
         * a dropped STATUS costs one 10 s interval */
    }
}

/* ------------------------------------------------------------------ */
/* Frame handling — loop() context                                     */
/* ------------------------------------------------------------------ */

static void resolve_pending(uint32_t ack_seq, bool ok, uint8_t reason)
{
    if ((s_txq_count == 0) || (s_txq[s_txq_head].seq != ack_seq)) {
        return;                       /* late ACK for an already-resolved frame */
    }
    const uint8_t cmd_type = s_txq[s_txq_head].type;
    hmi_link_evt_t e;
    memset(&e, 0, sizeof(e));
    e.cmd_type = cmd_type;
    e.cmd_seq = ack_seq;
    e.type = ok ? HMI_LINK_EVT_ACK_OK : HMI_LINK_EVT_NACK;
    e.nack_reason = reason;
    evt_push(&e);

    s_txq_head = (uint8_t)((s_txq_head + 1) % TXQ_DEPTH);
    s_txq_count--;
    s_last_ack_ms = millis();
}

static void handle_frame(const rx_item_t *it)
{
    const saq_frame_t *f = &it->f;

    switch (f->type) {
    case SAQ_T_ACK:
        resolve_pending(f->ack.ack_seq, true, 0);
        break;

    case SAQ_T_NACK:
        resolve_pending(f->ack.ack_seq, false, f->ack.result);
        break;

    case SAQ_T_STATUS: {
        /* Dedupe retries of the same uplink seq. */
        if (!saq_dedupe_check(&s_dedupe, f->seq)) {
            s_rx_dups++;
            return;
        }
        g_app.st_clock_conf = f->status.clock_conf;
        g_app.st_sched_count = f->status.sched_count;
        g_app.st_last_feed_epoch = f->status.last_feed_epoch;
        g_app.st_hopper_pct = f->status.hopper_pct;
        g_app.st_last_log_seq = f->status.last_log_seq;
        g_app.st_uptime_s = f->status.uptime_s;
        g_app.st_link_seq = f->status.link_seq;
        g_app.st_has_config = f->status.has_config;
        g_app.st_ms_per_g = f->status.ms_per_g;
        g_app.st_default_portion_g = f->status.default_portion_g;
        g_app.last_status_ms = millis();
        if (!s_link_up) {
            s_link_up = true;
            hmi_link_evt_t e;
            memset(&e, 0, sizeof(e));
            e.type = HMI_LINK_EVT_LINK_UP;
            evt_push(&e);
        }
        hmi_link_evt_t e;
        memset(&e, 0, sizeof(e));
        e.type = HMI_LINK_EVT_STATUS;
        evt_push(&e);
        break;
    }

    case SAQ_T_FEED_DONE: {
        if (!saq_dedupe_check(&s_dedupe, f->seq)) {
            s_rx_dups++;
            return;
        }
        hmi_link_evt_t e;
        memset(&e, 0, sizeof(e));
        e.type = HMI_LINK_EVT_FEED_DONE;
        e.feed_done.portion_g = f->feed_done.portion_g;
        e.feed_done.actual_ms = f->feed_done.actual_ms;
        e.feed_done.trigger = f->feed_done.trigger;
        e.feed_done.result = f->feed_done.result;
        evt_push(&e);
        break;
    }

    case SAQ_T_LOG_ENTRY: {
        if (!saq_dedupe_check(&s_dedupe, f->seq)) {
            s_rx_dups++;
            return;
        }
        hmi_link_evt_t e;
        memset(&e, 0, sizeof(e));
        e.type = HMI_LINK_EVT_LOG_ENTRY;
        e.log_entry.log_seq = f->log_entry.log_seq;
        e.log_entry.event_epoch = f->log_entry.event_epoch;
        e.log_entry.portion_g = f->log_entry.portion_g;
        e.log_entry.actual_ms = f->log_entry.actual_ms;
        e.log_entry.trigger = f->log_entry.trigger;
        e.log_entry.result = f->log_entry.result;
        evt_push(&e);
        break;
    }

    case SAQ_T_FAULT: {
        if (!saq_dedupe_check(&s_dedupe, f->seq)) {
            s_rx_dups++;
            return;
        }
        hmi_link_evt_t e;
        memset(&e, 0, sizeof(e));
        e.type = HMI_LINK_EVT_FAULT;
        e.fault.code = f->fault.code;
        e.fault.detail = f->fault.detail;
        evt_push(&e);
        break;
    }

    case SAQ_T_BEACON: {
        hmi_link_evt_t e;
        memset(&e, 0, sizeof(e));
        e.type = HMI_LINK_EVT_BEACON;
        memcpy(e.from_mac, it->from, 6);
        e.beacon.node_id = f->beacon.node_id;
        e.beacon.fw_major = f->beacon.fw_major;
        e.beacon.fw_minor = f->beacon.fw_minor;
        e.beacon.hopper_pct = f->beacon.hopper_pct;
        evt_push(&e);
        break;
    }

    default:
        break;                         /* uplink types we do not use */
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool link_init(void)
{
    saq_dedupe_init(&s_dedupe);

    s_rxq = xQueueCreate(16, sizeof(rx_item_t));
    s_evtq = xQueueCreate(EVTQ_DEPTH, sizeof(hmi_link_evt_t));
    if ((s_rxq == NULL) || (s_evtq == NULL)) {
        return false;
    }

    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(SAQ_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        return false;
    }
    esp_now_set_pmk(SAQ_PMK_BYTES);
    esp_now_register_recv_cb(on_recv);

    if (g_app.paired) {
        memcpy(s_peer_mac, g_app.peer_mac, 6);
        peer_add(true);                /* paired from NVS: encrypted from the start */
    }
    return true;
}

void link_set_pair_armed(bool armed)
{
    s_pair_armed = armed;
    s_pair_armed_ms = millis();
}

bool link_pair_armed(void)
{
    if (s_pair_armed && ((uint32_t)(millis() - s_pair_armed_ms) > (SAQ_PAIR_WINDOW_S * 1000u))) {
        s_pair_armed = false;          /* self-closing, like the feeder's window */
    }
    return s_pair_armed;
}

bool link_pair_to(const uint8_t mac[6])
{
    memcpy(s_peer_mac, mac, 6);
    peer_add(false);                   /* handshake foot: unencrypted */
    s_pair_armed = true;               /* keep beacons flowing until done */

    saq_frame_t f;
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_PAIR_REQ;
    f.flags = SAQ_FLAG_ACK_REQUIRED;
    s_tx_seq++;
    f.seq = s_tx_seq;
    f.epoch_s = model_epoch();
    f.pair_req.node_id = 1;
    return tx_push(&f);
}

bool link_finish_pairing(const uint8_t mac[6])
{
    memcpy(s_peer_mac, mac, 6);
    peer_add(true);                    /* upgrade: family LMK from now on */
    s_pair_armed = false;
    model_save_peer(mac);              /* persists and sets g_app.paired */
    s_link_up = false;

    /* Push the HMI's cached state, then reconcile history. Ordering is the
     * queue's problem, not the radio's — commands go out one at a time. */
    const uint32_t now = model_epoch();
    if (now != 0) {
        (void)link_send_set_time(now, model_tz_s());
    }
    (void)link_send_set_config(g_app.ms_per_g, g_app.default_portion_g);
    for (uint8_t i = 0; i < g_app.sched.count; i++) {
        (void)link_send_sched_entry(i, g_app.sched.count, &g_app.sched.entries[i]);
    }
    (void)link_send_status_req(g_app.hist_last_seq);
    return true;
}

void link_unpair(void)
{
    if (s_peer_added) {
        esp_now_del_peer(s_peer_mac);
        s_peer_added = false;
    }
    g_app.paired = false;
    s_link_up = false;
    s_txq_count = 0;
    /* The model keeps its history; only the peer identity is forgotten. */
}

void link_tick(void)
{
    /* 1. drain received frames first — a pending command's ACK may be
     *    sitting in the queue and must beat the retry timer. */
    if (s_rxq != NULL) {
        rx_item_t it;
        while (xQueueReceive(s_rxq, &it, 0) == pdTRUE) {
            handle_frame(&it);
        }
    }

    /* 2. drive the head command: send / retry / give up */
    if (s_txq_count > 0) {
        tx_cmd_t *c = &s_txq[s_txq_head];
        const uint32_t now = millis();

        if (c->attempts == 0) {
            if (s_peer_added && (esp_now_send(s_peer_mac, c->buf, c->len) == ESP_OK)) {
                c->attempts = 1;
                c->next_try_ms = now + SAQ_ACK_TIMEOUT_MS;
                s_tx_frames++;
            } else {
                /* nothing to send to (unpaired) — drop the queue loudly */
                hmi_link_evt_t e;
                memset(&e, 0, sizeof(e));
                e.type = HMI_LINK_EVT_TIMEOUT;
                e.cmd_type = c->type;
                e.cmd_seq = c->seq;
                evt_push(&e);
                s_txq_head = (uint8_t)((s_txq_head + 1) % TXQ_DEPTH);
                s_txq_count--;
                return;
            }
        } else if ((int32_t)(now - c->next_try_ms) >= 0) {
            if (c->attempts < SAQ_TX_ATTEMPTS) {
                /* same seq — that is what makes dedupe work at the far end */
                if (esp_now_send(s_peer_mac, c->buf, c->len) == ESP_OK) {
                    c->attempts++;
                    c->next_try_ms = now + SAQ_ACK_TIMEOUT_MS * c->attempts;
                    s_tx_retries++;
                    s_tx_frames++;
                }
            } else {
                hmi_link_evt_t e;
                memset(&e, 0, sizeof(e));
                e.type = HMI_LINK_EVT_TIMEOUT;
                e.cmd_type = c->type;
                e.cmd_seq = c->seq;
                evt_push(&e);
                s_txq_head = (uint8_t)((s_txq_head + 1) % TXQ_DEPTH);
                s_txq_count--;
            }
        }
    }

    /* 3. link freshness (docs/07: 3 missed STATUS intervals -> offline) */
    if (g_app.paired && s_link_up &&
        ((uint32_t)(millis() - g_app.last_status_ms) >
         (SAQ_STATUS_INTERVAL_S * 1000u * SAQ_LINK_LOST_INTERVALS))) {
        s_link_up = false;
        hmi_link_evt_t e;
        memset(&e, 0, sizeof(e));
        e.type = HMI_LINK_EVT_LINK_DOWN;
        evt_push(&e);
    }
    (void)link_pair_armed();
}

bool link_poll(hmi_link_evt_t *evt)
{
    if ((s_evtq == NULL) || (evt == NULL)) {
        return false;
    }
    return xQueueReceive(s_evtq, evt, 0) == pdTRUE;
}

hmi_cmd_state_t link_cmd_state(void)
{
    return (s_txq_count > 0) ? HMI_CMD_WAIT_ACK : HMI_CMD_IDLE;
}

bool link_send_feed(uint16_t portion_g, uint16_t forced_ms)
{
    saq_frame_t f;
    if (!cmd_begin(&f, SAQ_T_FEED)) {
        return false;
    }
    f.feed.portion_g = portion_g;
    f.feed.forced_ms = forced_ms;
    return tx_push(&f);
}

bool link_send_stop(void)
{
    saq_frame_t f;
    if (!cmd_begin(&f, SAQ_T_STOP)) {
        return false;
    }
    return tx_push(&f);
}

bool link_send_set_time(uint32_t utc_epoch, int32_t tz_s)
{
    saq_frame_t f;
    if (!cmd_begin(&f, SAQ_T_SET_TIME)) {
        return false;
    }
    f.set_time.epoch = utc_epoch;
    f.set_time.tz_offset_s = tz_s;
    return tx_push(&f);
}

bool link_send_sched_entry(uint8_t index, uint8_t count, const saq_sched_entry_t *entry)
{
    saq_frame_t f;
    if (!cmd_begin(&f, SAQ_T_SET_SCHEDULE_ENTRY)) {
        return false;
    }
    f.sched_entry.index = index;
    f.sched_entry.count = count;
    if (entry != NULL) {
        f.sched_entry.entry = *entry;
    }
    return tx_push(&f);
}

bool link_send_status_req(uint32_t since_seq)
{
    saq_frame_t f;
    if (!cmd_begin(&f, SAQ_T_STATUS_REQ)) {
        return false;
    }
    f.status_req.since_seq = since_seq;
    return tx_push(&f);
}

bool link_send_set_config(uint16_t ms_per_g, uint16_t default_portion_g)
{
    saq_frame_t f;
    if (!cmd_begin(&f, SAQ_T_SET_CONFIG)) {
        return false;
    }
    f.set_config.ms_per_g = ms_per_g;
    f.set_config.default_portion_g = default_portion_g;
    return tx_push(&f);
}

uint32_t link_tx_frames(void)      { return s_tx_frames; }
uint32_t link_tx_retries(void)     { return s_tx_retries; }
uint32_t link_rx_frames(void)      { return s_rx_frames; }
uint32_t link_rx_dups(void)        { return s_rx_dups; }
uint32_t link_rx_foreign(void)     { return s_rx_foreign; }
uint32_t link_last_ack_age_ms(void) { return (s_last_ack_ms == 0) ? 0 : (millis() - s_last_ack_ms); }
