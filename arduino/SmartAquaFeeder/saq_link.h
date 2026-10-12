/**
 * saq_link.h — feeder-side ESP-NOW transport for the SmartAqua protocol.
 *
 * Responsibilities (docs/07):
 *   - filter by peer MAC, drop unknown senders
 *   - dedupe the last 8 accepted downlink seqs; STOP never deduped, always
 *     acted on and always ACKed
 *   - ACK / NACK every ACK_REQUIRED downlink
 *   - run the pairing window: BEACON broadcast every second for 60 s after
 *     a 3 s button hold; accept PAIR_REQ only inside the window
 *   - send STATUS every SAQ_STATUS_INTERVAL_S while paired, plus log replay
 *     (LOG_ENTRY frames) when a STATUS_REQ asks for entries after a seq
 *
 * Threading: the ESP-NOW receive callback runs on the WiFi task. It decodes
 * (cheap), filters, and pushes the frame into a queue. Everything that
 * touches NVS, the relay, or the clock is executed later from loop() via
 * link_tick(). The single exception is STOP, whose GPIO de-energise path
 * (relay_emergency_off) is callback-safe and runs immediately.
 */

#pragma once

#include "saq_protocol.h"
#include "saq_schedule.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FEEDER_LINK_EVT_NONE = 0,
    FEEDER_LINK_EVT_PAIRED, /* pairing completed */
    FEEDER_LINK_EVT_PAIR_WINDOW_OPEN,
    FEEDER_LINK_EVT_PAIR_WINDOW_CLOSED,
} feeder_link_evt_t;

feeder_link_evt_t link_init(void); /* returns a pending event, if any */
void link_tick(void);              /* call from loop() every few ms */

/* Open the pairing window (60 s, self-closing). */
void link_open_pair_window(void);

bool link_paired(void);
bool link_pair_window_open(void);

/* Snapshot of the authoritative schedule (link owns the RAM copy so a
 * SET_SCHEDULE_ENTRY from the HMI is visible to the local scheduler on
 * the very next tick). */
void link_sched_snapshot(saq_sched_t *out);

/* Current feeding configuration (ms/g + default portion). */
void link_cfg(uint16_t *ms_per_g, uint16_t *default_portion_g);

/* Frame counters for the serial console / diagnostics. */
uint32_t link_rx_frames(void);
uint32_t link_rx_dups(void);
uint32_t link_rx_foreign(void); /* dropped: sender not the peer */
uint32_t link_tx_frames(void);
