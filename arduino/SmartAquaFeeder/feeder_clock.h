/**
 * feeder_clock.h — timekeeping on the feeder node (design rule 3).
 *
 * Sources, in order of trust:
 *   1. DS3231 (optional, battery-backed) — read at boot and hourly
 *   2. SET_TIME pushed from the HMI's PCF85063A
 *   3. nothing — clock_conf stays false and STATUS says so; scheduled
 *      feeds do not fire until a source appears, because feeding on a
 *      wrong clock is worse than not feeding.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void clock_init(void);
void clock_tick(void); /* call once a second from loop() */

bool clock_synced(void);          /* true once any source has set the time */
uint32_t clock_epoch(void);       /* UTC epoch seconds; 0 when unsynced */
uint32_t clock_local_epoch(void); /* UTC + tz offset — what the schedule runs on */
int32_t clock_tz_s(void);

/* Adopt a time push (SET_TIME) or an RTC reading. */
void clock_set(uint32_t utc_epoch, int32_t tz_s);

/* Human-readable "HH:MM:SS dd-mm-yy" for the serial console. */
void clock_str(char *buf, size_t cap);
