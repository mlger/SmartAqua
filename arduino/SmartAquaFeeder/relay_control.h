/**
 * relay_control.h — auger actuation with the fail-safe rules baked in.
 *
 * docs/01-architecture.md rule 2, quoted for posterity:
 *   "De-energised means motor off. A hard-coded maximum energised duration
 *    is enforced below the command layer, so no malformed command, hung
 *    task, or reboot can run the motor indefinitely."
 *
 * Consequences implemented here:
 *   - relay_init() drives the pin to OFF as its first and only action, and
 *     the sketch calls it before anything else in setup().
 *   - relay_start() refuses absurd durations before anything is energised.
 *   - the countdown is polled from loop(); a crash de-energises by losing
 *     power (NO contact), not by code.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Result of starting a feed. */
typedef enum {
    RELAY_START_OK = 0,
    RELAY_START_BUSY,          /* a feed is already running */
    RELAY_START_BAD_DURATION,  /* forced_ms out of range (0 only via FEED.forced_ms) */
} relay_start_result_t;

void relay_init(void);
void relay_tick(void);                       /* call from loop() every few ms */

/* Start a feed. `ms` is the requested energise duration; the clamp inside
 * is the one from saq_portion.h and it is not negotiable. `portion_g` and
 * `trigger` are recorded with the log entry. */
relay_start_result_t relay_start(uint16_t ms, uint16_t portion_g, uint8_t trigger);

/* Immediate de-energise. Safe to call any time, from any state, including
 * from the ESP-NOW receive callback (it only touches the GPIO). The log
 * entry is written later, from loop(), via relay_finalize(). */
void relay_emergency_off(void);

/* True while the relay is (or was — pending finalisation) energised. */
bool relay_active(void);

/* Milliseconds the current feed has run (0 when idle). */
uint32_t relay_elapsed_ms(void);

/* Boot-time recovery: turns a stale in-progress record into an
 * "interrupted" log entry (M2: a mid-feed reboot does not re-feed). */
void relay_recover_interrupted(void);

/* Test hook: asserts the clamp on an absurd duration. Returns the clamped
 * value so the bring-up checklist can print it. */
uint32_t relay_clamp_for_test(uint32_t absurd_ms);
