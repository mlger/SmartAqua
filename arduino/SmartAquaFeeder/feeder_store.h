/**
 * feeder_store.h — NVS persistence for the feeder node (Arduino Preferences).
 *
 * Namespaces and the `peer_mac` key follow docs/07 § Pairing ("Store both
 * MACs in NVS, in saq_cfg.peer_mac"). The schedule is authoritative here —
 * design rule 1: the feeder node is the record, the HMI is a view.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "saq_protocol.h"
#include "saq_schedule.h"
#include "saq_portion.h"

/* One retained feed-history entry — same fields as the LOG_ENTRY packet,
 * so replay is a mechanical copy. */
typedef struct {
    uint32_t log_seq;      /* assigned by the store on append */
    uint32_t event_epoch;  /* UTC epoch when the feed started; 0 if clock unsynced */
    uint16_t portion_g;    /* portion as requested (grams) */
    uint16_t actual_ms;    /* milliseconds the relay actually stayed energised */
    uint8_t  trigger;      /* SAQ_TRIGGER_* */
    uint8_t  result;       /* SAQ_FEED_* */
} feeder_log_entry_t;

/* Crash-safety record written BEFORE the relay energises and deleted after
 * (M2 acceptance: "feed_in_progress crash-safety flag; a mid-feed reboot
 * does not re-feed" — instead it logs an interrupted feed). */
typedef struct {
    uint8_t  magic;        /* 0xA5 when a record is live */
    uint8_t  trigger;
    uint16_t portion_g;
    uint16_t run_ms;       /* planned duration (already clamped) */
    uint32_t start_epoch;  /* UTC epoch at start, 0 if unsynced */
    uint32_t start_ms;     /* millis() at start */
} feeder_inprog_t;

#define FEEDER_INPROG_MAGIC 0xA5u

void store_init(void);

/* ---- schedule (authoritative copy) --------------------------------- */
bool store_load_sched(saq_sched_t *s);
bool store_save_sched(const saq_sched_t *s);

/* ---- feeding config ------------------------------------------------- */
void store_load_cfg(uint16_t *ms_per_g, uint16_t *default_portion_g);
void store_save_cfg(uint16_t ms_per_g, uint16_t default_portion_g);

/* ---- link ----------------------------------------------------------- */
bool store_load_peer(uint8_t mac[6]);
bool store_save_peer(const uint8_t mac[6]);
bool store_have_peer(void);

/* ---- timezone (seconds east of UTC, from SET_TIME) ------------------ */
int32_t store_load_tz(void);
void store_save_tz(int32_t tz_s);

/* ---- bounded feed log ----------------------------------------------- */
/* Appends and assigns log_seq. Returns false only on NVS failure. */
bool store_log_append(feeder_log_entry_t *e);

/* Copies entries with log_seq > since_seq (ascending) into out, up to max.
 * Returns the number copied — 0 when nothing new is retained. */
uint8_t store_log_replay(uint32_t since_seq, feeder_log_entry_t *out, uint8_t max);

uint32_t store_log_last_seq(void);

/* Most recent feed epoch (for STATUS.last_feed_epoch), 0 if none. */
uint32_t store_log_last_feed_epoch(void);

/* ---- crash-safety record -------------------------------------------- */
void store_set_inprog(const feeder_inprog_t *rec);
bool store_get_inprog(feeder_inprog_t *rec);   /* false when none live */
void store_clear_inprog(void);
