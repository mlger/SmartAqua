/**
 * feeder_store.cpp — see feeder_store.h.
 */

#include "feeder_store.h"
#include "feeder_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

static Preferences prefs;

/* Retained ring, held in RAM mirror and flushed to NVS on append. */
static feeder_log_entry_t s_ring[FEEDER_LOG_RING];
static uint32_t s_last_seq = 0;

void store_init(void)
{
    prefs.begin(FEEDER_NS_CFG, false);
    /* log lives in its own namespace so a schedule-heavy key set cannot
     * evict it — Preferences namespaces are independent NVS entries */
    prefs.end();

    prefs.begin(FEEDER_NS_LOG, false);
    s_last_seq = prefs.getUInt("log_seq", 0);
    uint8_t tmp[FEEDER_LOG_RING * sizeof(feeder_log_entry_t)];
    size_t got = prefs.getBytes("ring", tmp, sizeof(tmp));
    if (got == sizeof(tmp)) {
        memcpy(s_ring, tmp, sizeof(tmp));
    } else {
        memset(s_ring, 0, sizeof(s_ring));
    }
    prefs.end();
}

bool store_load_sched(saq_sched_t *s)
{
    if (s == NULL) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    prefs.begin(FEEDER_NS_CFG, true);
    uint8_t buf[SAQ_SCHED_MAX * 8 + 2];
    size_t got = prefs.getBytes("sched", buf, sizeof(buf));
    uint8_t count = prefs.getUChar("sched_n", 0);
    prefs.end();

    if ((got == (size_t)(count * 8)) && (count <= SAQ_SCHED_MAX)) {
        s->count = count;
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *p = buf + i * 8;
            s->entries[i].hour = p[0];
            s->entries[i].minute = p[1];
            s->entries[i].days = p[2];
            s->entries[i].flags = p[3];
            s->entries[i].portion_g = (uint16_t)(p[4] | (p[5] << 8));
            s->entries[i].pad = 0;
        }
        /* Refuse to act on a corrupt entry — rule: reject, never guess. */
        for (uint8_t i = 0; i < count; i++) {
            if (!saq_sched_entry_valid(&s->entries[i]) &&
                (s->entries[i].flags & SAQ_SCHED_FLAG_ENABLED)) {
                s->entries[i].flags &= (uint8_t)~SAQ_SCHED_FLAG_ENABLED;
            }
        }
    } else {
        s->count = 0;
    }
    return true;
}

bool store_save_sched(const saq_sched_t *s)
{
    if ((s == NULL) || (s->count > SAQ_SCHED_MAX)) {
        return false;
    }
    uint8_t buf[SAQ_SCHED_MAX * 8];
    for (uint8_t i = 0; i < s->count; i++) {
        uint8_t *p = buf + i * 8;
        const saq_sched_entry_t *e = &s->entries[i];
        p[0] = e->hour;
        p[1] = e->minute;
        p[2] = e->days;
        p[3] = e->flags;
        p[4] = (uint8_t)(e->portion_g & 0xFF);
        p[5] = (uint8_t)(e->portion_g >> 8);
        p[6] = 0;
        p[7] = 0;
    }
    prefs.begin(FEEDER_NS_CFG, false);
    prefs.putBytes("sched", buf, (size_t)s->count * 8);
    prefs.putUChar("sched_n", s->count);
    prefs.end();
    return true;
}

void store_load_cfg(uint16_t *ms_per_g, uint16_t *default_portion_g)
{
    prefs.begin(FEEDER_NS_CFG, true);
    uint16_t ms = prefs.getUShort("ms_per_g", SAQ_MS_PER_G_DEFAULT);
    uint16_t pg = prefs.getUShort("portion_g", 3);
    prefs.end();
    if (ms == 0) {
        ms = SAQ_MS_PER_G_DEFAULT;
    }
    if (pg < SAQ_PORTION_MIN_G) {
        pg = SAQ_PORTION_MIN_G;
    }
    if (pg > SAQ_PORTION_MAX_G) {
        pg = SAQ_PORTION_MAX_G;
    }
    if (ms_per_g != NULL) {
        *ms_per_g = ms;
    }
    if (default_portion_g != NULL) {
        *default_portion_g = pg;
    }
}

void store_save_cfg(uint16_t ms_per_g, uint16_t default_portion_g)
{
    prefs.begin(FEEDER_NS_CFG, false);
    prefs.putUShort("ms_per_g", ms_per_g);
    prefs.putUShort("portion_g", default_portion_g);
    prefs.end();
}

bool store_load_peer(uint8_t mac[6])
{
    uint8_t stored[6] = {0};
    prefs.begin(FEEDER_NS_CFG, true);
    size_t got = prefs.getBytes("peer_mac", stored, 6);
    prefs.end();
    if (got != 6) {
        return false;   /* never paired */
    }
    bool all_zero = true;
    for (int i = 0; i < 6; i++) {
        if (stored[i] != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        return false;
    }
    memcpy(mac, stored, 6);
    return true;
}

bool store_save_peer(const uint8_t mac[6])
{
    prefs.begin(FEEDER_NS_CFG, false);
    prefs.putBytes("peer_mac", mac, 6);
    prefs.end();
    return true;
}

bool store_have_peer(void)
{
    uint8_t mac[6];
    return store_load_peer(mac);
}

int32_t store_load_tz(void)
{
    prefs.begin(FEEDER_NS_CFG, true);
    int32_t tz = prefs.getInt("tz_s", 0);
    prefs.end();
    return tz;
}

void store_save_tz(int32_t tz_s)
{
    prefs.begin(FEEDER_NS_CFG, false);
    prefs.putInt("tz_s", tz_s);
    prefs.end();
}

bool store_log_append(feeder_log_entry_t *e)
{
    if (e == NULL) {
        return false;
    }
    s_last_seq++;
    e->log_seq = s_last_seq;

    uint8_t idx = (uint8_t)(s_last_seq % FEEDER_LOG_RING);
    memcpy(&s_ring[idx], e, sizeof(*e));

    prefs.begin(FEEDER_NS_LOG, false);
    prefs.putBytes("ring", s_ring, sizeof(s_ring));
    prefs.putUInt("log_seq", s_last_seq);
    prefs.end();
    return true;
}

static void log_entry_unused_removed(void) {}
uint8_t store_log_replay(uint32_t since_seq, feeder_log_entry_t *out, uint8_t max)
{
    if ((out == NULL) || (s_last_seq == 0)) {
        return 0;
    }
    uint8_t n = 0;
    /* ascending order: from the oldest retained seq > since_seq up to last */
    uint32_t oldest = (s_last_seq > FEEDER_LOG_RING) ? (s_last_seq - FEEDER_LOG_RING + 1) : 1;
    if (since_seq + 1 > oldest) {
        oldest = since_seq + 1;
    }
    for (uint32_t seq = oldest; (seq <= s_last_seq) && (n < max); seq++) {
        uint8_t idx = (uint8_t)(seq % FEEDER_LOG_RING);
        if (s_ring[idx].log_seq == seq) {
            memcpy(&out[n], &s_ring[idx], sizeof(out[n]));
            n++;
        }
    }
    return n;
}

uint32_t store_log_last_seq(void)
{
    return s_last_seq;
}

uint32_t store_log_last_feed_epoch(void)
{
    if (s_last_seq == 0) {
        return 0;
    }
    uint8_t idx = (uint8_t)(s_last_seq % FEEDER_LOG_RING);
    return s_ring[idx].event_epoch;
}

void store_set_inprog(const feeder_inprog_t *rec)
{
    prefs.begin(FEEDER_NS_CFG, false);
    prefs.putBytes("in_prog", rec, sizeof(*rec));
    prefs.end();
}

bool store_get_inprog(feeder_inprog_t *rec)
{
    prefs.begin(FEEDER_NS_CFG, true);
    feeder_inprog_t tmp;
    size_t got = prefs.getBytes("in_prog", &tmp, sizeof(tmp));
    prefs.end();
    if ((got != sizeof(tmp)) || (tmp.magic != FEEDER_INPROG_MAGIC)) {
        return false;
    }
    if (rec != NULL) {
        memcpy(rec, &tmp, sizeof(tmp));
    }
    return true;
}

void store_clear_inprog(void)
{
    prefs.begin(FEEDER_NS_CFG, false);
    prefs.remove("in_prog");
    prefs.end();
}
