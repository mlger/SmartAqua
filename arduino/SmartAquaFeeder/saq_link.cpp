/**
 * saq_link.cpp — feeder-side ESP-NOW engine. See saq_link.h.
 */

#include "saq_link.h"
#include "feeder_clock.h"
#include "feeder_config.h"
#include "feeder_store.h"
#include "relay_control.h"
#include "saq_portion.h"
#include "saq_schedule.h"

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

typedef struct {
    saq_frame_t f;
    uint8_t from[6];
} rx_item_t;

static bool s_paired = false;
static uint8_t s_peer_mac[6] = {0};
static bool s_peer_added = false;
static uint32_t s_tx_seq = 0;
static saq_dedupe_t s_dedupe;
static QueueHandle_t s_rxq = NULL;

static bool s_pair_window = false;
static uint32_t s_pair_window_ms = 0;
static uint32_t s_last_beacon_ms = 0;
static uint32_t s_last_status_ms = 0;
static feeder_link_evt_t s_pending_evt = FEEDER_LINK_EVT_NONE;

/* Authoritative in-RAM copies (NVS is the durable record). */
static saq_sched_t s_sched;
static uint16_t s_ms_per_g = SAQ_MS_PER_G_DEFAULT;
static uint16_t s_default_portion_g = 3;

/* Counters. */
static uint32_t s_rx_frames = 0, s_rx_dups = 0, s_rx_foreign = 0, s_tx_frames = 0;

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ------------------------------------------------------------------ */
/* Peer management                                                     */
/* ------------------------------------------------------------------ */

static void peer_add(bool encrypted) {
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
    if (esp_now_add_peer(&p) == ESP_OK) {
        s_peer_added = true;
    } else {
        Serial.println("[FEEDER] esp_now_add_peer failed");
    }
}

/* ------------------------------------------------------------------ */
/* Sending                                                             */
/* ------------------------------------------------------------------ */

static bool send_frame(saq_frame_t *f) {
    if (!s_peer_added) {
        return false;
    }
    uint8_t buf[SAQ_FRAME_MAX];
    const int n = saq_encode(buf, sizeof(buf), f);
    if (n <= 0) {
        return false;
    }
    if (esp_now_send(s_peer_mac, buf, (size_t)n) == ESP_OK) {
        s_tx_frames++;
        return true;
    }
    return false;
}

static uint32_t next_tx_seq(void) {
    s_tx_seq++;
    return s_tx_seq;
}

static void send_ack_for(const saq_frame_t *req, uint8_t result) {
    saq_frame_t f;
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_ACK;
    f.flags = SAQ_FLAG_IS_RESPONSE;
    f.seq = next_tx_seq();
    f.epoch_s = clock_epoch(); /* 0 when unsynced — informative */
    f.ack.ack_seq = req->seq;
    f.ack.result = result;
    send_frame(&f);
}

static void send_nack_for(const saq_frame_t *req, uint8_t reason) {
    saq_frame_t f;
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_NACK;
    f.flags = SAQ_FLAG_IS_RESPONSE;
    f.seq = next_tx_seq();
    f.epoch_s = clock_epoch();
    f.ack.ack_seq = req->seq;
    f.ack.result = reason;
    send_frame(&f);
}

static void send_status(void) {
    saq_frame_t f;
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_STATUS;
    f.flags = 0;
    f.seq = next_tx_seq();
    f.epoch_s = clock_epoch();
    f.status.clock_conf = clock_synced() ? 1 : 0;
    f.status.sched_count = s_sched.count;
    f.status.last_feed_epoch = store_log_last_feed_epoch();
    f.status.hopper_pct = SAQ_HOPPER_UNKNOWN; /* no sensor fitted (docs/09) */
    f.status.last_log_seq = store_log_last_seq();
    f.status.uptime_s = millis() / 1000u;
    f.status.link_seq = s_tx_seq;
    f.status.ms_per_g = s_ms_per_g;
    f.status.default_portion_g = s_default_portion_g;
    send_frame(&f);
}

static void send_beacon(void) {
    if (!s_peer_added) {
        return;
    }
    saq_frame_t f;
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_BEACON;
    f.flags = 0;
    f.seq = next_tx_seq();
    f.epoch_s = clock_epoch();
    f.beacon.node_id = SAQ_NODE_ID;
    f.beacon.fw_major = FEEDER_FW_MAJOR;
    f.beacon.fw_minor = FEEDER_FW_MINOR;
    f.beacon.hopper_pct = SAQ_HOPPER_UNKNOWN;

    uint8_t buf[SAQ_FRAME_MAX];
    const int n = saq_encode(buf, sizeof(buf), &f);
    if (n > 0) {
        if (esp_now_send(BROADCAST_MAC, buf, (size_t)n) == ESP_OK) {
            s_tx_frames++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Receive callback — WiFi task context: filter, decode, enqueue       */
/* ------------------------------------------------------------------ */

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if ((info == NULL) || (data == NULL) || (len <= 0)) {
        return;
    }

    const bool from_peer = (memcmp(info->src_addr, s_peer_mac, 6) == 0);
    if (!from_peer && !s_pair_window) {
        s_rx_foreign++; /* docs/07: drop frames from unknown senders */
        return;
    }

    saq_frame_t f;
    if (saq_decode(data, (size_t)len, &f) != SAQ_CODEC_OK) {
        return; /* reject, never guess — and count nothing */
    }
    s_rx_frames++;

    /* STOP: the one frame that acts immediately. The GPIO write is
     * callback-safe; logging and the ACK happen from loop(). */
    if (f.type == SAQ_T_STOP) {
        relay_emergency_off();
    }

    rx_item_t it;
    it.f = f;
    memcpy(it.from, info->src_addr, 6);
    if (xQueueSend(s_rxq, &it, 0) != pdTRUE) {
        /* queue full: drop rather than block the WiFi task. The HMI's
         * retry (same seq) will get it through. */
    }
}

/* ------------------------------------------------------------------ */
/* Command handling — loop() context                                   */
/* ------------------------------------------------------------------ */

static void handle_feed(const saq_frame_t *f) {
    /* Calibration test feeds arrive as portion 0 + forced_ms; normal feeds
     * carry a portion and let the node's calibration decide the duration. */
    if (f->feed.forced_ms == 0) {
        if ((f->feed.portion_g < SAQ_PORTION_MIN_G) || (f->feed.portion_g > SAQ_PORTION_MAX_G)) {
            send_nack_for(f, SAQ_RES_BAD_PAYLOAD);
            return;
        }
    }
    if (f->feed.forced_ms > SAQ_MAX_FEED_MS) {
        send_nack_for(f, SAQ_RES_REJECTED_DURATION);
        return;
    }

    const uint16_t ms =
        (f->feed.forced_ms != 0) ? f->feed.forced_ms : (uint16_t)saq_portion_ms(f->feed.portion_g, s_ms_per_g);

    const relay_start_result_t r = relay_start(ms, f->feed.portion_g, SAQ_TRIGGER_MANUAL_HMI);
    if (r == RELAY_START_BUSY) {
        send_nack_for(f, SAQ_RES_BUSY);
        return;
    }
    send_ack_for(f, SAQ_RES_OK);
}

static void handle_set_time(const saq_frame_t *f) {
    if (f->set_time.epoch == 0) {
        /* A zero epoch means the sender's clock is unsynced (docs/07) —
         * there is nothing usable to adopt. */
        send_nack_for(f, SAQ_RES_CLOCK_UNSYNCED);
        return;
    }
    clock_set(f->set_time.epoch, f->set_time.tz_offset_s);
    char tbuf[24];
    clock_str(tbuf, sizeof(tbuf));
    Serial.printf("[FEEDER] SET_TIME -> %s\n", tbuf);
    send_ack_for(f, SAQ_RES_OK);
}

static void handle_set_sched_entry(const saq_frame_t *f) {
    const uint8_t idx = f->sched_entry.index;
    const uint8_t count = f->sched_entry.count;
    if ((count > SAQ_SCHED_MAX) || (count == 0) || (idx >= count)) {
        send_nack_for(f, SAQ_RES_BAD_PAYLOAD);
        return;
    }
    if (!saq_sched_entry_valid(&f->sched_entry.entry)) {
        send_nack_for(f, SAQ_RES_BAD_PAYLOAD);
        return;
    }
    /* `count` is the authoritative slot count after this write; entries
     * beyond the written index keep their stored values. */
    if (count > s_sched.count) {
        s_sched.count = count;
    }
    s_sched.entries[idx] = f->sched_entry.entry;
    if (!store_save_sched(&s_sched)) {
        send_nack_for(f, SAQ_RES_BAD_PAYLOAD);
        return;
    }
    Serial.printf("[FEEDER] schedule slot %u/%u = %02u:%02u, %u g\n", (unsigned)idx, (unsigned)count,
                  (unsigned)f->sched_entry.entry.hour, (unsigned)f->sched_entry.entry.minute,
                  (unsigned)f->sched_entry.entry.portion_g);
    send_ack_for(f, SAQ_RES_OK);
}

static void handle_status_req(const saq_frame_t *f) {
    send_ack_for(f, SAQ_RES_OK);

    /* Replay retained entries newer than since_seq, then the status frame.
     * Bounded batches keep the queue of the HMI's recv callback happy. */
    feeder_log_entry_t batch[8];
    uint32_t since = f->status_req.since_seq;
    for (;;) {
        const uint8_t n = store_log_replay(since, batch, 8);
        for (uint8_t i = 0; i < n; i++) {
            saq_frame_t out;
            memset(&out, 0, sizeof(out));
            out.type = SAQ_T_LOG_ENTRY;
            out.flags = 0;
            out.seq = next_tx_seq();
            out.epoch_s = clock_epoch();
            out.log_entry.log_seq = batch[i].log_seq;
            out.log_entry.event_epoch = batch[i].event_epoch;
            out.log_entry.portion_g = batch[i].portion_g;
            out.log_entry.actual_ms = batch[i].actual_ms;
            out.log_entry.trigger = batch[i].trigger;
            out.log_entry.result = batch[i].result;
            send_frame(&out);
            since = batch[i].log_seq;
        }
        if (n < 8) {
            break;
        }
    }
    send_status();
}

static void handle_pair_req(const saq_frame_t *f, const uint8_t from[6]) {
    /* Accepted only inside the pairing window — enforced by the filter in
     * on_recv (frames from unknown MACs are dropped otherwise). */
    if (f->pair_req.node_id != SAQ_NODE_ID) {
        return; /* beacon for someone else's feeder */
    }

    memcpy(s_peer_mac, from, 6);
    store_save_peer(s_peer_mac);

    /* Handshake order (docs/07 § Encryption + the key-upgrade note in
     * arduino/README.md): the HMI sends PAIR_REQ unencrypted, so it is
     * delivered even before this side knows the sender. ACK on the same
     * (unencrypted) foot, then upgrade the peer to the family LMK. */
    peer_add(false);
    s_paired = true;
    s_pair_window = false;
    s_pending_evt = FEEDER_LINK_EVT_PAIRED;

    send_ack_for(f, SAQ_RES_OK);
    peer_add(true);
    send_status();

    Serial.printf("[FEEDER] paired with HMI %02X:%02X:%02X:%02X:%02X:%02X\n", s_peer_mac[0], s_peer_mac[1],
                  s_peer_mac[2], s_peer_mac[3], s_peer_mac[4], s_peer_mac[5]);
}

static void handle_set_config(const saq_frame_t *f) {
    if ((f->set_config.ms_per_g < 100) || (f->set_config.ms_per_g > SAQ_MAX_FEED_MS) ||
        (f->set_config.default_portion_g < SAQ_PORTION_MIN_G) ||
        (f->set_config.default_portion_g > SAQ_PORTION_MAX_G)) {
        send_nack_for(f, SAQ_RES_BAD_PAYLOAD);
        return;
    }
    s_ms_per_g = f->set_config.ms_per_g;
    s_default_portion_g = f->set_config.default_portion_g;
    store_save_cfg(s_ms_per_g, s_default_portion_g);
    Serial.printf("[FEEDER] config: %u ms/g, default %u g\n", (unsigned)s_ms_per_g, (unsigned)s_default_portion_g);
    send_ack_for(f, SAQ_RES_OK);
}

static void handle_frame(rx_item_t *it) {
    saq_frame_t *f = &it->f;

    /* Dedupe FIRST — a retry must not feed twice, but must be ACKed again
     * (docs/07 § "Sequence numbers, retries, dedupe"). STOP is exempt. */
    if (f->type != SAQ_T_STOP) {
        if (!saq_dedupe_check(&s_dedupe, f->seq)) {
            s_rx_dups++;
            if (f->flags & SAQ_FLAG_ACK_REQUIRED) {
                send_ack_for(f, SAQ_RES_OK);
            }
            return;
        }
    }

    switch (f->type) {
    case SAQ_T_FEED:
        handle_feed(f);
        break;
    case SAQ_T_STOP:
        relay_emergency_off(); /* idempotent; the callback already fired */
        send_ack_for(f, SAQ_RES_OK);
        break;
    case SAQ_T_SET_TIME:
        handle_set_time(f);
        break;
    case SAQ_T_SET_SCHEDULE_ENTRY:
        handle_set_sched_entry(f);
        break;
    case SAQ_T_STATUS_REQ:
        handle_status_req(f);
        break;
    case SAQ_T_PAIR_REQ:
        handle_pair_req(f, it->from);
        break;
    case SAQ_T_SET_CONFIG:
        handle_set_config(f);
        break;
    default:
        /* Decode rejects unknown types; reaching here means a type that is
         * valid on the wire but not implemented by this firmware. */
        if (f->flags & SAQ_FLAG_ACK_REQUIRED) {
            send_nack_for(f, SAQ_RES_UNSUPPORTED_TYPE);
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

feeder_link_evt_t link_init(void) {
    memset(&s_sched, 0, sizeof(s_sched));
    store_load_sched(&s_sched);
    store_load_cfg(&s_ms_per_g, &s_default_portion_g);
    saq_dedupe_init(&s_dedupe);

    s_rxq = xQueueCreate(16, sizeof(rx_item_t));
    if (s_rxq == NULL) {
        Serial.println("[FEEDER] rx queue alloc failed");
    }

    /* Mode A: no router, fixed channel (docs/07). */
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(SAQ_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[FEEDER] esp_now_init failed");
        return FEEDER_LINK_EVT_NONE;
    }
    esp_now_set_pmk(SAQ_PMK_BYTES);
    esp_now_register_recv_cb(on_recv);

    /* Broadcast peer carries the unencrypted BEACON during pairing. */
    {
        esp_now_peer_info_t p;
        memset(&p, 0, sizeof(p));
        memcpy(p.peer_addr, BROADCAST_MAC, 6);
        p.channel = SAQ_ESPNOW_CHANNEL;
        p.ifidx = WIFI_IF_STA;
        p.encrypt = false;
        esp_now_add_peer(&p);
    }

    if (store_load_peer(s_peer_mac)) {
        peer_add(true); /* already paired: encrypted from the start */
        s_paired = true;
        Serial.printf("[FEEDER] paired from NVS: %02X:%02X:%02X:%02X:%02X:%02X\n", s_peer_mac[0], s_peer_mac[1],
                      s_peer_mac[2], s_peer_mac[3], s_peer_mac[4], s_peer_mac[5]);
    } else {
        Serial.println("[FEEDER] not paired — hold the button 3 s to pair");
    }
    return FEEDER_LINK_EVT_NONE;
}

void link_open_pair_window(void) {
    s_pair_window = true;
    s_pair_window_ms = millis();
    s_pending_evt = FEEDER_LINK_EVT_PAIR_WINDOW_OPEN;
    Serial.println("[FEEDER] pairing window open for 60 s");
}

void link_tick(void) {
    /* Pairing window bookkeeping */
    if (s_pair_window) {
        if ((uint32_t)(millis() - s_pair_window_ms) > (SAQ_PAIR_WINDOW_S * 1000u)) {
            s_pair_window = false;
            s_pending_evt = FEEDER_LINK_EVT_PAIR_WINDOW_CLOSED;
            Serial.println("[FEEDER] pairing window closed");
        } else if ((uint32_t)(millis() - s_last_beacon_ms) >= SAQ_BEACON_PERIOD_MS) {
            s_last_beacon_ms = millis();
            send_beacon();
        }
    }

    /* Periodic status while paired */
    if (s_paired && !s_pair_window) {
        if ((uint32_t)(millis() - s_last_status_ms) >= (SAQ_STATUS_INTERVAL_S * 1000u)) {
            s_last_status_ms = millis();
            send_status();
        }
    }

    /* Drain the receive queue */
    if (s_rxq != NULL) {
        rx_item_t it;
        while (xQueueReceive(s_rxq, &it, 0) == pdTRUE) {
            handle_frame(&it);
        }
    }
}

bool link_paired(void) {
    return s_paired;
}

bool link_pair_window_open(void) {
    return s_pair_window;
}

void link_sched_snapshot(saq_sched_t *out) {
    if (out != NULL) {
        *out = s_sched;
    }
}

void link_cfg(uint16_t *ms_per_g, uint16_t *default_portion_g) {
    if (ms_per_g != NULL) {
        *ms_per_g = s_ms_per_g;
    }
    if (default_portion_g != NULL) {
        *default_portion_g = s_default_portion_g;
    }
}

uint32_t link_rx_frames(void) {
    return s_rx_frames;
}
uint32_t link_rx_dups(void) {
    return s_rx_dups;
}
uint32_t link_rx_foreign(void) {
    return s_rx_foreign;
}
uint32_t link_tx_frames(void) {
    return s_tx_frames;
}
