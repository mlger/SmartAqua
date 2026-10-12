/**
 * saq_schedule.h — schedule storage, matching and "next feed" arithmetic.
 *
 * Pure C99, no platform headers — mirrors components/smartaqua_schedule from
 * the repo docs (docs/01-architecture.md § "Where things live in code").
 *
 * All times passed here are LOCAL seconds since the Unix epoch (the caller
 * adds the timezone offset before calling). Day bits follow the wire format:
 * bit0 = Sunday .. bit6 = Saturday.
 */

#ifndef SAQ_SCHEDULE_H
#define SAQ_SCHEDULE_H

#include "saq_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1970-01-01 was a Thursday; with Sunday = 0 this gives weekday 4. */
#define SAQ_EPOCH_WDAY_OFFSET 4u

typedef struct {
    saq_sched_entry_t entries[SAQ_SCHED_MAX];
    uint8_t count;
} saq_sched_t;

/* ---- calendar helpers on local epoch seconds ---------------------- */

uint8_t saq_wday_of(uint32_t local_epoch_s); /* 0 = Sunday */
uint8_t saq_hour_of(uint32_t local_epoch_s);
uint8_t saq_minute_of(uint32_t local_epoch_s);
uint32_t saq_epoch_min_of(uint32_t local_epoch_s); /* local_epoch / 60 */
uint32_t saq_day_index_of(uint32_t local_epoch_s); /* local_epoch / 86400 */

/* ---- validation ---------------------------------------------------- */

/**
 * A valid entry: enabled flag clean, hour < 24, minute < 60, at least one
 * day, portion within 1..SAQ_PORTION_MAX_G. Used by both the editor (HMI)
 * and the receiver (feeder refuses invalid SET_SCHEDULE_ENTRY payloads).
 */
bool saq_sched_entry_valid(const saq_sched_entry_t *e);

/* ---- matching (feeder side) ---------------------------------------- */

/**
 * Index of the first entry due at local_epoch_s, given the per-entry
 * last-fired minute stamps (epoch minutes; 0 = never fired this boot).
 * Due means: enabled, today's day bit set, hh:mm matches, and this exact
 * minute has not already fired for that entry.
 *
 * Call repeatedly while it returns >= 0 (and update the stamp when firing)
 * so two entries at the same minute both feed.
 */
int saq_sched_find_due(const saq_sched_entry_t *entries, uint8_t count, const uint32_t *last_fired_min,
                       uint32_t local_epoch_s);

/* ---- "next feed" (HMI side) ---------------------------------------- */

/**
 * Next scheduled feed strictly after local_epoch_s.
 * @param out_epoch_s  local epoch second of the next due minute
 * @param out_index    which entry
 * @return true if a due entry exists, false if no enabled future feed
 *         (empty schedule, or all entries disabled).
 * Searches up to 8 days ahead (7 days + today) — bounded, no loops forever
 * on a disabled-but-present schedule.
 */
bool saq_sched_next_due(const saq_sched_entry_t *entries, uint8_t count, uint32_t local_epoch_s, uint32_t *out_epoch_s,
                        uint8_t *out_index);

/* ---- display helpers (HMI side) ------------------------------------ */

/** "Every day", "Mon Wed Fri", "Weekends", "Sun only", ... into buf. */
void saq_sched_days_summary(uint8_t days, char *buf, size_t cap);

/** "HH:MM" of an entry into buf (cap >= 6). */
void saq_sched_time_str(const saq_sched_entry_t *e, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* SAQ_SCHEDULE_H */
