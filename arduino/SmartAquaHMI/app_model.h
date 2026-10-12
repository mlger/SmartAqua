/**
 * app_model.h — the HMI's world view: cached feeder state, schedule copy,
 * history, settings, and the NVS persistence under it.
 *
 * The feeder node is authoritative (design rule 1); everything here is a
 * cache that reconciles from STATUS / LOG_ENTRY traffic and is pushed back
 * with SET_* commands. History merges live FEED_DONE packets with replayed
 * LOG_ENTRY packets, deduped by log_seq.
 */

#pragma once

#include "saq_protocol.h"
#include "saq_schedule.h"
#include <stdbool.h>
#include <stdint.h>

#define HMI_NS_CFG  "saq_cfg"  /* peer_mac, tz_h, portion_g, ms_per_g */
#define HMI_NS_HIST "saq_hist" /* count, ring, last_seq */

/* One history entry — same shape as the feeder's log and LOG_ENTRY. */
typedef struct {
    uint32_t log_seq;
    uint32_t event_epoch; /* UTC; 0 when the feeder's clock was unsynced */
    uint16_t portion_g;
    uint16_t actual_ms;
    uint8_t trigger; /* SAQ_TRIGGER_* */
    uint8_t result;  /* SAQ_FEED_* */
} saq_hist_entry_t;

#define HMI_HIST_MAX 64

typedef struct {
    /* ---- link ---- */
    bool paired;
    uint8_t peer_mac[6];
    bool link_up; /* STATUS within the last 3 intervals */
    uint32_t last_status_ms;

    /* ---- last feeder STATUS (fields as decoded) ---- */
    uint8_t st_clock_conf;
    uint8_t st_sched_count;
    uint32_t st_last_feed_epoch;
    uint8_t st_hopper_pct;
    uint32_t st_last_log_seq;
    uint32_t st_uptime_s;
    uint32_t st_link_seq;
    bool st_has_config;
    uint16_t st_ms_per_g;
    uint16_t st_default_portion_g;
    uint8_t feeder_fw_major, feeder_fw_minor;

    /* ---- schedule cache ---- */
    saq_sched_t sched;

    /* ---- settings (NVS-backed) ---- */
    int32_t tz_h; /* hours, for the settings roller */
    uint16_t default_portion_g;
    uint16_t ms_per_g;

    /* ---- history ---- */
    saq_hist_entry_t hist[HMI_HIST_MAX];
    uint16_t hist_count;
    uint32_t hist_last_seq;

    /* ---- clock ---- */
    bool clock_ok; /* RTC sane */
} app_model_t;

extern app_model_t g_app;

void model_init(void);

/* Time. model_local_epoch() is what the schedule editor and the Home
 * screen reason in; it is UTC + tz. */
uint32_t model_epoch(void);
uint32_t model_local_epoch(void);
int32_t model_tz_s(void);

/* History: merge one entry (dedupes by log_seq, keeps newest first).
 * Returns true when the entry was new. */
bool model_hist_add(const saq_hist_entry_t *e);

/* Settings changes (also persist). */
void model_set_tz(int32_t tz_h);
void model_set_cfg(uint16_t ms_per_g, uint16_t default_portion_g);

/* Peer management. */
bool model_load_peer(void);
void model_save_peer(const uint8_t mac[6]);
