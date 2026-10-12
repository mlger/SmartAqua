/**
 * saq_protocol.cpp — codec + dedupe + self-test for the SmartAqua wire
 * protocol v1.1. See saq_protocol.h and docs/07-wireless-protocol.md.
 */

#include "saq_protocol.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Little-endian helpers — explicit offsets, never struct memcpy        */
/* ------------------------------------------------------------------ */

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Required payload length per type. -1 = variable (STATUS), -2 = unknown. */
static int payload_len_for_type(uint8_t type) {
    switch (type) {
    case SAQ_T_FEED:
        return 4;
    case SAQ_T_STOP:
        return 0;
    case SAQ_T_SET_TIME:
        return 8;
    case SAQ_T_SET_SCHEDULE_ENTRY:
        return 10;
    case SAQ_T_STATUS_REQ:
        return 4;
    case SAQ_T_PAIR_REQ:
        return 1;
    case SAQ_T_SET_CONFIG:
        return 4;
    case SAQ_T_ACK:
        return 5;
    case SAQ_T_NACK:
        return 5;
    case SAQ_T_STATUS:
        return 19; /* v1.0 minimum; v1.1 sends 23 */
    case SAQ_T_FEED_DONE:
        return 6;
    case SAQ_T_FAULT:
        return 4;
    case SAQ_T_BEACON:
        return 4;
    case SAQ_T_LOG_ENTRY:
        return 14; /* 4+4+2+2+1+1 */
    default:
        return -2;
    }
}

/* ------------------------------------------------------------------ */
/* Encode                                                              */
/* ------------------------------------------------------------------ */

int saq_encode_v(uint8_t *out, size_t cap, const saq_frame_t *f, uint8_t version) {
    if ((out == NULL) || (f == NULL) || (cap < SAQ_FRAME_HEADER_SIZE)) {
        return SAQ_CODEC_ERR_ARG;
    }

    uint8_t buf[SAQ_MAX_PAYLOAD];
    size_t plen = 0;

    switch (f->type) {
    case SAQ_T_FEED:
        plen = 4;
        put_u16(buf + 0, f->feed.portion_g);
        put_u16(buf + 2, f->feed.forced_ms);
        break;
    case SAQ_T_STOP:
        plen = 0;
        break;
    case SAQ_T_SET_TIME:
        plen = 8;
        put_u32(buf + 0, f->set_time.epoch);
        put_u32(buf + 4, (uint32_t)f->set_time.tz_offset_s);
        break;
    case SAQ_T_SET_SCHEDULE_ENTRY: {
        plen = 10;
        buf[0] = f->sched_entry.index;
        buf[1] = f->sched_entry.count;
        const saq_sched_entry_t *e = &f->sched_entry.entry;
        buf[2] = e->hour;
        buf[3] = e->minute;
        buf[4] = e->days;
        buf[5] = e->flags;
        put_u16(buf + 6, e->portion_g);
        put_u16(buf + 8, 0); /* reserved */
        break;
    }
    case SAQ_T_STATUS_REQ:
        plen = 4;
        put_u32(buf + 0, f->status_req.since_seq);
        break;
    case SAQ_T_PAIR_REQ:
        plen = 1;
        buf[0] = f->pair_req.node_id;
        break;
    case SAQ_T_SET_CONFIG:
        plen = 4;
        put_u16(buf + 0, f->set_config.ms_per_g);
        put_u16(buf + 2, f->set_config.default_portion_g);
        break;
    case SAQ_T_ACK:
    case SAQ_T_NACK:
        plen = 5;
        put_u32(buf + 0, f->ack.ack_seq);
        buf[4] = f->ack.result;
        break;
    case SAQ_T_STATUS:
        /* v1.1 sends the 4-byte config tail; a v1.0 frame carries the
         * original 19-byte payload exactly (docs/07 vector 5 pins this). */
        plen = (version >= SAQ_PROTO_VERSION_BYTE) ? 23 : 19;
        buf[0] = f->status.clock_conf;
        buf[1] = f->status.sched_count;
        put_u32(buf + 2, f->status.last_feed_epoch);
        buf[6] = f->status.hopper_pct;
        put_u32(buf + 7, f->status.last_log_seq);
        put_u32(buf + 11, f->status.uptime_s);
        put_u32(buf + 15, f->status.link_seq);
        if (plen >= 23) {
            put_u16(buf + 19, f->status.ms_per_g);
            put_u16(buf + 21, f->status.default_portion_g);
        }
        break;
    case SAQ_T_FEED_DONE:
        plen = 6;
        put_u16(buf + 0, f->feed_done.portion_g);
        put_u16(buf + 2, f->feed_done.actual_ms);
        buf[4] = f->feed_done.trigger;
        buf[5] = f->feed_done.result;
        break;
    case SAQ_T_FAULT:
        plen = 4;
        put_u16(buf + 0, f->fault.code);
        put_u16(buf + 2, f->fault.detail);
        break;
    case SAQ_T_BEACON:
        plen = 4;
        buf[0] = f->beacon.node_id;
        buf[1] = f->beacon.fw_major;
        buf[2] = f->beacon.fw_minor;
        buf[3] = f->beacon.hopper_pct;
        break;
    case SAQ_T_LOG_ENTRY:
        plen = 14;
        put_u32(buf + 0, f->log_entry.log_seq);
        put_u32(buf + 4, f->log_entry.event_epoch);
        put_u16(buf + 8, f->log_entry.portion_g);
        put_u16(buf + 10, f->log_entry.actual_ms);
        buf[12] = f->log_entry.trigger;
        buf[13] = f->log_entry.result;
        break;
    default:
        return SAQ_CODEC_ERR_TYPE;
    }

    if (cap < SAQ_FRAME_HEADER_SIZE + plen) {
        return SAQ_CODEC_ERR_ARG;
    }

    out[0] = SAQ_MAGIC;
    out[1] = version;
    out[2] = f->type;
    out[3] = f->flags;
    put_u32(out + 4, f->seq);
    put_u32(out + 8, f->epoch_s);
    if (plen > 0) {
        memcpy(out + SAQ_FRAME_HEADER_SIZE, buf, plen);
    }
    return (int)(SAQ_FRAME_HEADER_SIZE + plen);
}

/* ------------------------------------------------------------------ */
/* Decode                                                              */
/* ------------------------------------------------------------------ */

saq_codec_err_t saq_decode(const uint8_t *in, size_t len, saq_frame_t *out) {
    if ((in == NULL) || (out == NULL)) {
        return SAQ_CODEC_ERR_ARG;
    }
    if (len < SAQ_FRAME_HEADER_SIZE) {
        return SAQ_CODEC_ERR_TRUNCATED;
    }

    if (in[0] != SAQ_MAGIC) {
        return SAQ_CODEC_ERR_MAGIC;
    }
    const uint8_t major = (uint8_t)(in[1] >> 4);
    if (major != SAQ_PROTO_VERSION_MAJOR) {
        return SAQ_CODEC_ERR_VERSION;
    }

    const uint8_t type = in[2];
    const int req = payload_len_for_type(type);
    if (req == -2) {
        return SAQ_CODEC_ERR_TYPE;
    }

    const size_t plen = len - SAQ_FRAME_HEADER_SIZE;
    if (plen > SAQ_MAX_PAYLOAD) {
        return SAQ_CODEC_ERR_OVERSIZE;
    }

    const uint8_t *p = in + SAQ_FRAME_HEADER_SIZE;

    /* STATUS is the one variable-length payload: >= 19, config tail at 19..23 */
    size_t need = (size_t)req;
    if (type == SAQ_T_STATUS) {
        need = 19;
    }
    if (plen < need) {
        return SAQ_CODEC_ERR_TRUNCATED;
    }

    memset(out, 0, sizeof(*out));
    out->type = type;
    out->flags = in[3];
    out->seq = get_u32(in + 4);
    out->epoch_s = get_u32(in + 8);
    out->payload_len = (uint16_t)plen;

    switch (type) {
    case SAQ_T_FEED:
        out->feed.portion_g = get_u16(p + 0);
        out->feed.forced_ms = get_u16(p + 2);
        break;
    case SAQ_T_STOP:
        break;
    case SAQ_T_SET_TIME:
        out->set_time.epoch = get_u32(p + 0);
        out->set_time.tz_offset_s = (int32_t)get_u32(p + 4);
        break;
    case SAQ_T_SET_SCHEDULE_ENTRY: {
        out->sched_entry.index = p[0];
        out->sched_entry.count = p[1];
        saq_sched_entry_t *e = &out->sched_entry.entry;
        e->hour = p[2];
        e->minute = p[3];
        e->days = p[4];
        e->flags = p[5];
        e->portion_g = get_u16(p + 6);
        e->pad = get_u16(p + 8);
        break;
    }
    case SAQ_T_STATUS_REQ:
        out->status_req.since_seq = get_u32(p + 0);
        break;
    case SAQ_T_PAIR_REQ:
        out->pair_req.node_id = p[0];
        break;
    case SAQ_T_SET_CONFIG:
        out->set_config.ms_per_g = get_u16(p + 0);
        out->set_config.default_portion_g = get_u16(p + 2);
        break;
    case SAQ_T_ACK:
    case SAQ_T_NACK:
        out->ack.ack_seq = get_u32(p + 0);
        out->ack.result = p[4];
        break;
    case SAQ_T_STATUS:
        out->status.clock_conf = p[0];
        out->status.sched_count = p[1];
        out->status.last_feed_epoch = get_u32(p + 2);
        out->status.hopper_pct = p[6];
        out->status.last_log_seq = get_u32(p + 7);
        out->status.uptime_s = get_u32(p + 11);
        out->status.link_seq = get_u32(p + 15);
        if (plen >= 23) {
            out->status.has_config = true;
            out->status.ms_per_g = get_u16(p + 19);
            out->status.default_portion_g = get_u16(p + 21);
        }
        break;
    case SAQ_T_FEED_DONE:
        out->feed_done.portion_g = get_u16(p + 0);
        out->feed_done.actual_ms = get_u16(p + 2);
        out->feed_done.trigger = p[4];
        out->feed_done.result = p[5];
        break;
    case SAQ_T_FAULT:
        out->fault.code = get_u16(p + 0);
        out->fault.detail = get_u16(p + 2);
        break;
    case SAQ_T_BEACON:
        out->beacon.node_id = p[0];
        out->beacon.fw_major = p[1];
        out->beacon.fw_minor = p[2];
        out->beacon.hopper_pct = p[3];
        break;
    case SAQ_T_LOG_ENTRY:
        out->log_entry.log_seq = get_u32(p + 0);
        out->log_entry.event_epoch = get_u32(p + 4);
        out->log_entry.portion_g = get_u16(p + 8);
        out->log_entry.actual_ms = get_u16(p + 10);
        out->log_entry.trigger = p[12];
        out->log_entry.result = p[13];
        break;
    default:
        return SAQ_CODEC_ERR_TYPE; /* unreachable; keeps the switch exhaustive */
    }

    return SAQ_CODEC_OK;
}

/* ------------------------------------------------------------------ */
/* Dedupe window                                                       */
/* ------------------------------------------------------------------ */

void saq_dedupe_init(saq_dedupe_t *w) {
    memset(w, 0, sizeof(*w));
}

bool saq_dedupe_check(saq_dedupe_t *w, uint32_t seq) {
    for (uint8_t i = 0; i < w->count; i++) {
        if (w->seqs[i] == seq) {
            return false; /* duplicate: re-ACK, do not act */
        }
    }
    w->seqs[w->pos] = seq;
    w->pos = (uint8_t)((w->pos + 1) % SAQ_DEDUPE_WINDOW);
    if (w->count < SAQ_DEDUPE_WINDOW) {
        w->count++;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Self-test — docs/07 § "Test vectors"                                */
/* ------------------------------------------------------------------ */

typedef struct {
    int failures;
    char *buf;
    size_t cap;
    size_t used;
} test_report_t;

static void rpt(test_report_t *r, const char *line) {
    if ((r->buf != NULL) && (r->used + 2 < r->cap)) {
        int n = snprintf(r->buf + r->used, r->cap - r->used, "%s\n", line);
        if (n > 0) {
            r->used += (size_t)n;
            if (r->used > r->cap - 1) {
                r->used = r->cap - 1;
            }
        }
    }
}

static void check(test_report_t *r, const char *name, bool ok) {
    char line[96];
    snprintf(line, sizeof(line), "%s %s", ok ? "ok  " : "FAIL", name);
    rpt(r, line);
    if (!ok) {
        r->failures++;
    }
}

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    return memcmp(a, b, n) == 0;
}

int saq_protocol_self_test(char *report, size_t report_cap) {
    test_report_t r = {0, report, report_cap, 0};
    if ((report != NULL) && (report_cap > 0)) {
        report[0] = '\0';
    }

    uint8_t out[SAQ_FRAME_MAX];
    saq_frame_t f, d;

    /* ---- vector 1: FEED seq 42, epoch 1735689600, 3 g, forced 0 ---- */
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_FEED;
    f.flags = SAQ_FLAG_ACK_REQUIRED;
    f.seq = 42;
    f.epoch_s = 1735689600u;
    f.feed.portion_g = 3;
    f.feed.forced_ms = 0;
    int n = saq_encode_v(out, sizeof(out), &f, 0x10);
    {
        static const uint8_t exp[16] = {0xA5, 0x10, 0x10, 0x01, 0x2A, 0x00, 0x00, 0x00,
                                        0x80, 0x85, 0x74, 0x67, 0x03, 0x00, 0x00, 0x00};
        check(&r, "v1 FEED bytes", (n == 16) && bytes_equal(out, exp, 16));
    }
    check(&r, "v1 FEED decode",
          (saq_decode(out, (size_t)n, &d) == SAQ_CODEC_OK) && (d.type == SAQ_T_FEED) && (d.seq == 42) &&
              (d.epoch_s == 1735689600u) && (d.feed.portion_g == 3) && (d.feed.forced_ms == 0));

    /* ---- vector 2: STOP seq 43, epoch 1735689601 (header-only) ---- */
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_STOP;
    f.flags = SAQ_FLAG_ACK_REQUIRED;
    f.seq = 43;
    f.epoch_s = 1735689601u;
    n = saq_encode_v(out, sizeof(out), &f, 0x10);
    {
        static const uint8_t exp[12] = {0xA5, 0x10, 0x11, 0x01, 0x2B, 0x00, 0x00, 0x00, 0x81, 0x85, 0x74, 0x67};
        check(&r, "v2 STOP bytes (12, header-only)", (n == 12) && bytes_equal(out, exp, 12));
    }

    /* ---- vector 3: ACK seq 1, epoch 1735689600, ack_seq 42, result 0 ---- */
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_ACK;
    f.flags = SAQ_FLAG_IS_RESPONSE;
    f.seq = 1;
    f.epoch_s = 1735689600u;
    f.ack.ack_seq = 42;
    f.ack.result = SAQ_RES_OK;
    n = saq_encode_v(out, sizeof(out), &f, 0x10);
    {
        static const uint8_t exp[17] = {0xA5, 0x10, 0x80, 0x02, 0x01, 0x00, 0x00, 0x00, 0x80,
                                        0x85, 0x74, 0x67, 0x2A, 0x00, 0x00, 0x00, 0x00};
        check(&r, "v3 ACK bytes", (n == 17) && bytes_equal(out, exp, 17));
    }

    /* ---- vector 4: ACK, epoch 0 — unsynced is data, not an error ---- */
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_ACK;
    f.flags = SAQ_FLAG_IS_RESPONSE;
    f.seq = 1;
    f.epoch_s = 0;
    f.ack.ack_seq = 42;
    f.ack.result = SAQ_RES_OK;
    n = saq_encode_v(out, sizeof(out), &f, 0x10);
    {
        static const uint8_t exp[17] = {0xA5, 0x10, 0x80, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00};
        check(&r, "v4 ACK no-clock bytes", (n == 17) && bytes_equal(out, exp, 17));
        check(&r, "v4 epoch 0 decodes OK", saq_decode(out, (size_t)n, &d) == SAQ_CODEC_OK);
    }

    /* ---- vector 5: STATUS, 19-byte v1.0 payload ---- */
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_STATUS;
    f.flags = 0;
    f.seq = 7;
    f.epoch_s = 1735689600u;
    f.status.clock_conf = 1;
    f.status.sched_count = 4;
    f.status.last_feed_epoch = 1735603200u;
    f.status.hopper_pct = 80;
    f.status.last_log_seq = 12;
    f.status.uptime_s = 3600;
    f.status.link_seq = 7;
    f.status.ms_per_g = 0; /* v1.0 frame: tail absent */
    f.status.default_portion_g = 0;
    n = saq_encode_v(out, sizeof(out), &f, 0x10);
    {
        /* v1.0 STATUS payload is exactly the first 19 bytes of the v1.1
         * payload; trim the 4-byte tail to reproduce vector 5. */
        uint8_t exp[31] = {0xA5, 0x10, 0x90, 0x00, 0x07, 0x00, 0x00, 0x00, 0x80, 0x85, 0x74,
                           0x67, 0x01, 0x04, 0x00, 0x34, 0x73, 0x67, 0x50, 0x0C, 0x00, 0x00,
                           0x00, 0x10, 0x0E, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00};
        check(&r, "v5 STATUS v1.0 bytes (31)", (n == 31) && bytes_equal(out, exp, 31));
        check(&r, "v5 STATUS decode, no config tail",
              (saq_decode(out, 31, &d) == SAQ_CODEC_OK) && !d.status.has_config && (d.status.clock_conf == 1) &&
                  (d.status.sched_count == 4) && (d.status.last_feed_epoch == 1735603200u) &&
                  (d.status.hopper_pct == 80) && (d.status.last_log_seq == 12) && (d.status.uptime_s == 3600) &&
                  (d.status.link_seq == 7));
    }

    /* ---- v1.1: extended STATUS (23-byte payload), vector 12 ---- */
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_STATUS;
    f.seq = 7;
    f.epoch_s = 1735689600u;
    f.status.clock_conf = 1;
    f.status.sched_count = 4;
    f.status.last_feed_epoch = 1735603200u;
    f.status.hopper_pct = 80;
    f.status.last_log_seq = 12;
    f.status.uptime_s = 3600;
    f.status.link_seq = 7;
    f.status.ms_per_g = 6000;
    f.status.default_portion_g = 3;
    n = saq_encode(out, sizeof(out), &f);
    {
        static const uint8_t exp[35] = {0xA5, 0x11, 0x90, 0x00, 0x07, 0x00, 0x00, 0x00, 0x80, 0x85, 0x74, 0x67,
                                        0x01, 0x04, 0x00, 0x34, 0x73, 0x67, 0x50, 0x0C, 0x00, 0x00, 0x00, 0x10,
                                        0x0E, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x70, 0x17, 0x03, 0x00};
        check(&r, "v12 STATUS v1.1 bytes (35)", (n == 35) && bytes_equal(out, exp, 35));
    }
    check(&r, "v1.1 STATUS config tail",
          (saq_decode(out, (size_t)n, &d) == SAQ_CODEC_OK) && d.status.has_config && (d.status.ms_per_g == 6000) &&
              (d.status.default_portion_g == 3));
    /* A v1.1 STATUS cut back to its first 31 bytes must still decode as the
     * v1.0 layout — trailing-byte tolerance in both directions. */
    check(&r, "v1.1 STATUS truncated to v1.0 still decodes",
          (saq_decode(out, 31, &d) == SAQ_CODEC_OK) && !d.status.has_config);

    /* ---- v1.1: SET_CONFIG, vector 10 ---- */
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_SET_CONFIG;
    f.flags = SAQ_FLAG_ACK_REQUIRED;
    f.seq = 44;
    f.epoch_s = 1735689600u;
    f.set_config.ms_per_g = 6000;
    f.set_config.default_portion_g = 3;
    n = saq_encode(out, sizeof(out), &f);
    {
        static const uint8_t exp[16] = {0xA5, 0x11, 0x16, 0x01, 0x2C, 0x00, 0x00, 0x00,
                                        0x80, 0x85, 0x74, 0x67, 0x70, 0x17, 0x03, 0x00};
        check(&r, "v10 SET_CONFIG bytes", (n == 16) && bytes_equal(out, exp, 16));
    }

    /* ---- v1.1: LOG_ENTRY, vector 11 ---- */
    memset(&f, 0, sizeof(f));
    f.type = SAQ_T_LOG_ENTRY;
    f.seq = 8;
    f.epoch_s = 1735689600u;
    f.log_entry.log_seq = 13;
    f.log_entry.event_epoch = 1735603200u;
    f.log_entry.portion_g = 3;
    f.log_entry.actual_ms = 18000;
    f.log_entry.trigger = SAQ_TRIGGER_SCHEDULED;
    f.log_entry.result = SAQ_FEED_COMPLETED;
    n = saq_encode(out, sizeof(out), &f);
    {
        static const uint8_t exp[26] = {0xA5, 0x11, 0x94, 0x00, 0x08, 0x00, 0x00, 0x00, 0x80, 0x85, 0x74, 0x67, 0x0D,
                                        0x00, 0x00, 0x00, 0x00, 0x34, 0x73, 0x67, 0x03, 0x00, 0x50, 0x46, 0x00, 0x00};
        check(&r, "v11 LOG_ENTRY bytes (26)", (n == 26) && bytes_equal(out, exp, 26));
    }
    check(&r, "v1.1 LOG_ENTRY decode",
          (saq_decode(out, (size_t)n, &d) == SAQ_CODEC_OK) && (d.log_entry.log_seq == 13) &&
              (d.log_entry.event_epoch == 1735603200u) && (d.log_entry.portion_g == 3) &&
              (d.log_entry.actual_ms == 18000) && (d.log_entry.trigger == SAQ_TRIGGER_SCHEDULED) &&
              (d.log_entry.result == SAQ_FEED_COMPLETED));

    /* ---- vector 6: bad magic ---- */
    {
        uint8_t bad[16] = {0xB4, 0x10, 0x10, 0x01, 0x2A, 0x00, 0x00, 0x00,
                           0x80, 0x85, 0x74, 0x67, 0x03, 0x00, 0x00, 0x00};
        check(&r, "v6 bad magic rejected", saq_decode(bad, sizeof(bad), &d) == SAQ_CODEC_ERR_MAGIC);
    }

    /* ---- vector 7: major version mismatch (0x20) ---- */
    {
        uint8_t bad[16] = {0xA5, 0x20, 0x10, 0x01, 0x2A, 0x00, 0x00, 0x00,
                           0x80, 0x85, 0x74, 0x67, 0x03, 0x00, 0x00, 0x00};
        check(&r, "v7 major mismatch rejected", saq_decode(bad, sizeof(bad), &d) == SAQ_CODEC_ERR_VERSION);
    }

    /* ---- vector 8: unknown type (0x77) ---- */
    {
        uint8_t bad[16] = {0xA5, 0x10, 0x77, 0x01, 0x2A, 0x00, 0x00, 0x00,
                           0x80, 0x85, 0x74, 0x67, 0x03, 0x00, 0x00, 0x00};
        check(&r, "v8 unknown type rejected", saq_decode(bad, sizeof(bad), &d) == SAQ_CODEC_ERR_TYPE);
    }

    /* ---- truncation and oversize ---- */
    check(&r, "truncated FEED rejected", saq_decode(out, 15, &d) == SAQ_CODEC_ERR_TRUNCATED); /* header + 3B < 4B */
    {
        /* 12-byte header + 239 bytes of payload = oversize */
        uint8_t big[SAQ_FRAME_HEADER_SIZE + SAQ_MAX_PAYLOAD + 1] = {0};
        big[0] = SAQ_MAGIC;
        big[1] = 0x10;
        big[2] = SAQ_T_FEED;
        big[3] = 0x01;
        check(&r, "oversize payload rejected", saq_decode(big, sizeof(big), &d) == SAQ_CODEC_ERR_OVERSIZE);
    }

    /* ---- vector 9: dedupe — a duplicate FEED feeds once, ACKs twice ---- */
    {
        saq_dedupe_t w;
        saq_dedupe_init(&w);
        check(&r, "v9 dedupe first seq 42 acts", saq_dedupe_check(&w, 42) == true);
        check(&r, "v9 dedupe repeat seq 42 does not act", saq_dedupe_check(&w, 42) == false);
        /* fill the window, then the oldest seq falls out and is treated as
         * new — the caller's high-water mark is the real guard there */
        for (uint32_t s = 100; s < 100 + SAQ_DEDUPE_WINDOW; s++) {
            (void)saq_dedupe_check(&w, s);
        }
        check(&r, "v9 dedupe window evicts oldest", saq_dedupe_check(&w, 42) == true);
    }

    /* ---- schedule entry wire roundtrip ---- */
    {
        saq_frame_t sf, sd;
        memset(&sf, 0, sizeof(sf));
        sf.type = SAQ_T_SET_SCHEDULE_ENTRY;
        sf.flags = SAQ_FLAG_ACK_REQUIRED;
        sf.seq = 9;
        sf.epoch_s = 1;
        sf.sched_entry.index = 2;
        sf.sched_entry.count = 3;
        sf.sched_entry.entry.hour = 8;
        sf.sched_entry.entry.minute = 30;
        sf.sched_entry.entry.days = SAQ_DAY_MON | SAQ_DAY_WED | SAQ_DAY_FRI;
        sf.sched_entry.entry.flags = SAQ_SCHED_FLAG_ENABLED;
        sf.sched_entry.entry.portion_g = 3;
        int m = saq_encode(out, sizeof(out), &sf);
        check(&r, "sched entry wire roundtrip",
              (m == 22) && (saq_decode(out, (size_t)m, &sd) == SAQ_CODEC_OK) && (sd.sched_entry.index == 2) &&
                  (sd.sched_entry.count == 3) && (sd.sched_entry.entry.hour == 8) &&
                  (sd.sched_entry.entry.minute == 30) &&
                  (sd.sched_entry.entry.days == (SAQ_DAY_MON | SAQ_DAY_WED | SAQ_DAY_FRI)) &&
                  (sd.sched_entry.entry.flags == SAQ_SCHED_FLAG_ENABLED) && (sd.sched_entry.entry.portion_g == 3));
    }

    {
        char line[64];
        snprintf(line, sizeof(line), "protocol self-test: %d failure(s)", r.failures);
        rpt(&r, line);
    }
    return r.failures;
}
