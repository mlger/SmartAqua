/**
 * saq_link.h — HMI-side ESP-NOW transport for the SmartAqua protocol.
 *
 * Differences from the feeder side (docs/07):
 *   - the HMI SENDS the ACK_REQUIRED commands, so it owns the retry state
 *     machine: 3 attempts, same seq, 250 ms timeout, 250/500 ms backoff
 *   - it never claims success without an ACK ("the HMI never reports a
 *     feed it has not seen acknowledged")
 *   - pairing runs from a BEACON: accept a candidate, add the peer
 *     unencrypted for the PAIR_REQ handshake, then upgrade to the family
 *     LMK once the ACK lands (the upgrade dance is documented in
 *     arduino/README.md)
 *   - link-up/down is derived from STATUS freshness (3 x 10 s)
 *
 * The engine is LVGL-free: events are drained from loop() with
 * link_poll() and the caller decides what to redraw, under the LVGL lock.
 */

#pragma once

#include "saq_protocol.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HMI_LINK_EVT_NONE = 0,
    HMI_LINK_EVT_ACK_OK,    /* pending command accepted */
    HMI_LINK_EVT_NACK,      /* pending command refused; see .nack_reason */
    HMI_LINK_EVT_TIMEOUT,   /* no ACK after 3 attempts — never report success */
    HMI_LINK_EVT_STATUS,    /* fresh STATUS merged into the model */
    HMI_LINK_EVT_FEED_DONE, /* .feed_done fields */
    HMI_LINK_EVT_LOG_ENTRY, /* .log_entry fields (reconciliation) */
    HMI_LINK_EVT_FAULT,     /* .fault fields */
    HMI_LINK_EVT_BEACON,    /* .beacon fields + .from_mac (pairing candidate) */
    HMI_LINK_EVT_LINK_UP,
    HMI_LINK_EVT_LINK_DOWN,
    HMI_LINK_EVT_PAIRED, /* pairing completed */
} hmi_link_evt_type_t;

typedef struct {
    hmi_link_evt_type_t type;
    uint8_t cmd_type; /* for ACK/NACK/TIMEOUT: which command */
    uint32_t cmd_seq;
    uint8_t nack_reason;
    uint8_t from_mac[6];
    union {
        struct {
            uint16_t portion_g, actual_ms;
            uint8_t trigger, result;
        } feed_done;
        struct {
            uint32_t log_seq, event_epoch;
            uint16_t portion_g, actual_ms;
            uint8_t trigger, result;
        } log_entry;
        struct {
            uint16_t code, detail;
        } fault;
        struct {
            uint8_t node_id, fw_major, fw_minor, hopper_pct;
        } beacon;
    };
} hmi_link_evt_t;

bool link_init(void);
void link_tick(void); /* drive retries + freshness, call from loop() */

/* Returns true when an event was copied out. Drain in a loop. */
bool link_poll(hmi_link_evt_t *evt);

/* ---- commands (one ACK_REQUIRED command in flight at a time) -------- */

typedef enum {
    HMI_CMD_IDLE = 0,
    HMI_CMD_WAIT_ACK, /* frame sent, waiting / retrying */
    HMI_CMD_DONE,
} hmi_cmd_state_t;

hmi_cmd_state_t link_cmd_state(void);

bool link_send_feed(uint16_t portion_g, uint16_t forced_ms);
bool link_send_stop(void);
bool link_send_set_time(uint32_t utc_epoch, int32_t tz_s);
bool link_send_sched_entry(uint8_t index, uint8_t count, const saq_sched_entry_t *entry);
bool link_send_status_req(uint32_t since_seq);
bool link_send_set_config(uint16_t ms_per_g, uint16_t default_portion_g);

/* Pair with the feeder whose BEACON we accepted (its MAC). */
bool link_pair_to(const uint8_t mac[6]);

/* Called when PAIR_REQ's ACK arrives: upgrades the peer to encrypted and
 * pushes the HMI's cached state (time, feeding config, schedule) plus a
 * STATUS_REQ to reconcile history. */
bool link_finish_pairing(const uint8_t mac[6]);

/* Arm/disarm beacon acceptance — armed for 60 s when the user starts the
 * pairing flow, so stray beacons cannot pair themselves. */
void link_set_pair_armed(bool armed);
bool link_pair_armed(void);

/* Forget the peer (Settings → Re-pair → confirm). */
void link_unpair(void);

/* Counters for Diagnostics. */
uint32_t link_tx_frames(void);
uint32_t link_tx_retries(void);
uint32_t link_rx_frames(void);
uint32_t link_rx_dups(void);
uint32_t link_rx_foreign(void);
uint32_t link_last_ack_age_ms(void);
