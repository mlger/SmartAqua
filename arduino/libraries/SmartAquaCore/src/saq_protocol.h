/**
 * saq_protocol.h — SmartAqua ESP-NOW wire protocol (v1.1)
 *
 * Contract: docs/07-wireless-protocol.md. Both firmwares implement this file.
 * If they disagree, fish go unfed.
 *
 * v1.1 (this implementation) adds, additively and per the documented change
 * rules (docs/07 § "Changing the protocol"):
 *   - downlink SET_CONFIG (0x16)
 *   - uplink   LOG_ENTRY (0x94) — log replay for history reconciliation
 *   - STATUS payload extended with a 4-byte tail (ms_per_g, default_portion_g).
 *     A v1.0 STATUS (19-byte payload) still decodes; trailing bytes are
 *     ignored by older decoders by design.
 *
 * Portability: this file (and its .cpp) is standard C99 — no ESP-IDF, no
 * Arduino headers — so it unit-tests on the host and is reused verbatim by
 * both sketches through the SmartAquaCore Arduino library.
 *
 * Encoding rules (docs/07 § "Frame format"):
 *   - little-endian throughout
 *   - explicit offsets, never a struct memcpy
 *   - reject, never guess: bad magic / major mismatch / unknown type → drop
 */

#ifndef SAQ_PROTOCOL_H
#define SAQ_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Versioning                                                          */
/* ------------------------------------------------------------------ */

#define SAQ_MAGIC 0xA5u

#define SAQ_PROTO_VERSION_MAJOR 1u
#define SAQ_PROTO_VERSION_MINOR 1u
/** Full byte on the wire: high nibble major, low nibble minor. */
#define SAQ_PROTO_VERSION_BYTE 0x11u

/* ------------------------------------------------------------------ */
/* Frame geometry                                                      */
/* ------------------------------------------------------------------ */

#define SAQ_FRAME_HEADER_SIZE 12u
#define SAQ_MAX_PAYLOAD       238u
#define SAQ_FRAME_MAX         (SAQ_FRAME_HEADER_SIZE + SAQ_MAX_PAYLOAD)

/* flags */
#define SAQ_FLAG_ACK_REQUIRED 0x01u
#define SAQ_FLAG_IS_RESPONSE  0x02u

/* ------------------------------------------------------------------ */
/* Packet types                                                        */
/* ------------------------------------------------------------------ */

/* downlink — HMI -> feeder */
#define SAQ_T_FEED               0x10u
#define SAQ_T_STOP               0x11u
#define SAQ_T_SET_TIME           0x12u
#define SAQ_T_SET_SCHEDULE_ENTRY 0x13u
#define SAQ_T_STATUS_REQ         0x14u
#define SAQ_T_PAIR_REQ           0x15u
#define SAQ_T_SET_CONFIG         0x16u /* v1.1 */

/* uplink — feeder -> HMI */
#define SAQ_T_ACK       0x80u
#define SAQ_T_NACK      0x81u
#define SAQ_T_STATUS    0x90u
#define SAQ_T_FEED_DONE 0x91u
#define SAQ_T_FAULT     0x92u
#define SAQ_T_BEACON    0x93u
#define SAQ_T_LOG_ENTRY 0x94u /* v1.1 */

/* ------------------------------------------------------------------ */
/* Result codes (ACK.result / NACK.reason)                             */
/* ------------------------------------------------------------------ */

#define SAQ_RES_OK                0x00u
#define SAQ_RES_REJECTED_DURATION 0x01u
#define SAQ_RES_BUSY              0x02u
#define SAQ_RES_UNSUPPORTED_TYPE  0x03u
#define SAQ_RES_BAD_PAYLOAD       0x04u
#define SAQ_RES_NOT_PAIRED        0x05u
#define SAQ_RES_CLOCK_UNSYNCED    0x06u

/* Feed outcome codes (FEED_DONE.result / LOG_ENTRY.result) — v1.1 names */
#define SAQ_FEED_COMPLETED   0x00u
#define SAQ_FEED_CLAMPED     0x01u /* truncated by the hard MAX_FEED_MS clamp */
#define SAQ_FEED_INTERRUPTED 0x02u /* rebooted mid-feed */
#define SAQ_FEED_STOPPED     0x03u /* stopped by a STOP command */

/* trigger codes (FEED_DONE.trigger / LOG_ENTRY.trigger) */
#define SAQ_TRIGGER_SCHEDULED    0x00u
#define SAQ_TRIGGER_MANUAL_HMI   0x01u
#define SAQ_TRIGGER_LOCAL_BUTTON 0x02u

/* hopper sentinel: 255 = no sensor fitted, NOT empty */
#define SAQ_HOPPER_UNKNOWN 255u

/* ------------------------------------------------------------------ */
/* Protocol timing constants (shared behaviour, docs/07)               */
/* ------------------------------------------------------------------ */

#define SAQ_ACK_TIMEOUT_MS      250u  /* wait for ACK before retransmit */
#define SAQ_TX_ATTEMPTS         3u    /* attempts, same seq, backoff 250ms*attempt */
#define SAQ_DEDUPE_WINDOW       8u    /* last N accepted seqs kept per direction */
#define SAQ_STATUS_INTERVAL_S   10u   /* feeder's unsolicited STATUS period */
#define SAQ_LINK_LOST_INTERVALS 3u    /* HMI marks node offline after N missed */
#define SAQ_PAIR_WINDOW_S       60u   /* pairing window opened by button hold */
#define SAQ_BEACON_PERIOD_MS    1000u /* beacon cadence inside the window */

/* ------------------------------------------------------------------ */
/* Schedule entry — 8 bytes on the wire (SET_SCHEDULE_ENTRY payload)   */
/* ------------------------------------------------------------------ */

#define SAQ_DAY_SUN (1u << 0)
#define SAQ_DAY_MON (1u << 1)
#define SAQ_DAY_TUE (1u << 2)
#define SAQ_DAY_WED (1u << 3)
#define SAQ_DAY_THU (1u << 4)
#define SAQ_DAY_FRI (1u << 5)
#define SAQ_DAY_SAT (1u << 6)
#define SAQ_DAY_ALL 0x7Fu

#define SAQ_SCHED_FLAG_ENABLED (1u << 0)
#define SAQ_SCHED_MAX          8u

/* Portion bounds — shared by the editor validation, the schedule entry
 * validity check, and the portion math. Grams, inclusive range. */
#define SAQ_PORTION_MIN_G 1u
#define SAQ_PORTION_MAX_G 10u

typedef struct {
    uint8_t hour;       /* 0..23, local time */
    uint8_t minute;     /* 0..59, local time */
    uint8_t days;       /* SAQ_DAY_* bitmask, bit0 = Sunday */
    uint8_t flags;      /* SAQ_SCHED_FLAG_ENABLED */
    uint16_t portion_g; /* grams, 1..SAQ_PORTION_MAX_G */
    uint16_t pad;       /* reserved, 0 on the wire */
} saq_sched_entry_t;    /* sizeof == 8 */

/* ------------------------------------------------------------------ */
/* Codec                                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    SAQ_CODEC_OK = 0,
    SAQ_CODEC_ERR_MAGIC = -1,     /* documented reject: bad magic */
    SAQ_CODEC_ERR_VERSION = -2,   /* documented reject: major mismatch */
    SAQ_CODEC_ERR_TYPE = -3,      /* documented reject: unknown type */
    SAQ_CODEC_ERR_TRUNCATED = -4, /* payload shorter than the type requires */
    SAQ_CODEC_ERR_OVERSIZE = -5,  /* payload longer than SAQ_MAX_PAYLOAD */
    SAQ_CODEC_ERR_ARG = -6,       /* null/buffer-too-small argument */
} saq_codec_err_t;

/**
 * Decoded frame. Scalar payload fields are copied out; unknown-but-tolerated
 * trailing payload bytes are ignored (forward compatibility, docs/07).
 */
typedef struct {
    uint8_t type;
    uint8_t flags;
    uint32_t seq;
    uint32_t epoch_s;     /* 0 = sender's clock unsynced */
    uint16_t payload_len; /* as received (may exceed the fields below) */
    union {
        struct {
            uint16_t portion_g, forced_ms;
        } feed;
        struct {
            uint32_t epoch;
            int32_t tz_offset_s;
        } set_time;
        struct {
            uint8_t index, count;
            saq_sched_entry_t entry;
        } sched_entry;
        struct {
            uint32_t since_seq;
        } status_req;
        struct {
            uint8_t node_id;
        } pair_req;
        struct {
            uint16_t ms_per_g, default_portion_g;
        } set_config;
        struct {
            uint32_t ack_seq;
            uint8_t result;
        } ack; /* ACK and NACK */
        struct {
            uint8_t clock_conf, sched_count;
            uint32_t last_feed_epoch;
            uint8_t hopper_pct;
            uint32_t last_log_seq;
            uint32_t uptime_s;
            uint32_t link_seq;
            /* v1.1 tail — zeroed when the sender omitted them */
            bool has_config;
            uint16_t ms_per_g, default_portion_g;
        } status;
        struct {
            uint16_t portion_g, actual_ms;
            uint8_t trigger, result;
        } feed_done;
        struct {
            uint16_t code, detail;
        } fault;
        struct {
            uint8_t node_id, fw_major, fw_minor, hopper_pct;
        } beacon;
        struct {
            uint32_t log_seq;
            uint32_t event_epoch;
            uint16_t portion_g, actual_ms;
            uint8_t trigger, result;
        } log_entry;
    };
} saq_frame_t;

/**
 * Encode a frame.
 * @param version  protocol version byte to stamp (SAQ_PROTO_VERSION_BYTE in
 *                 production; the self-test pins 0x10 to reproduce the
 *                 documented v1.0 test vectors byte-for-byte).
 * @return frame length in bytes (>= SAQ_FRAME_HEADER_SIZE) on success,
 *         or a negative saq_codec_err_t.
 */
int saq_encode_v(uint8_t *out, size_t cap, const saq_frame_t *f, uint8_t version);

/** Encode with the current protocol version. */
#define saq_encode(out, cap, f) saq_encode_v(out, cap, f, SAQ_PROTO_VERSION_BYTE)

/**
 * Decode a frame. Accepts any minor version with matching major (1).
 * @return SAQ_CODEC_OK or a negative saq_codec_err_t (reject, never guess).
 */
saq_codec_err_t saq_decode(const uint8_t *in, size_t len, saq_frame_t *out);

/* ------------------------------------------------------------------ */
/* Dedupe window — last SAQ_DEDUPE_WINDOW accepted seqs per direction  */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t seqs[SAQ_DEDUPE_WINDOW];
    uint8_t count;
    uint8_t pos;
} saq_dedupe_t;

void saq_dedupe_init(saq_dedupe_t *w);

/**
 * @return true  — seq is new (act on it, then remember it)
 *         false — seq already in the window (re-ACK, do not act)
 * Seqs older than the window are treated as new; the caller drops them by
 * comparing against its own high-water mark if it cares. STOP bypasses
 * dedupe entirely (idempotent by design) — that check is the caller's.
 */
bool saq_dedupe_check(saq_dedupe_t *w, uint32_t seq);

/* ------------------------------------------------------------------ */
/* Self-test — the test vectors from docs/07 § "Test vectors"          */
/* ------------------------------------------------------------------ */

/**
 * Runs the documented vectors plus reject and dedupe cases. Writes a
 * human-readable report (truncated to fit). Runs in ~1 ms; call it once at
 * boot from either sketch and log the result — a codec regression then
 * shows up on the serial console instead of as a mis-fed tank.
 *
 * @return number of failures (0 = pass).
 */
int saq_protocol_self_test(char *report, size_t report_cap);

#ifdef __cplusplus
}
#endif

#endif /* SAQ_PROTOCOL_H */
