/**
 * saq_rtc.h — PCF85063A driver for the HMI board (I2C address 0x51).
 *
 * The RTC hangs on the same I2C bus (SDA 8 / SCL 9, port 0) as the GT911
 * touch controller and the CH422G expander. ESP32_Display_Panel installs
 * the legacy IDF I2C driver on that port, so this driver talks to the chip
 * through the same driver instance (i2c_master_write_to_device and
 * friends take the port number, not a private handle) — no second bus,
 * no Wire.begin conflict.
 *
 * The RTC stores UTC. Local time = UTC + the timezone offset kept in the
 * app model. Register map and BCD layout per the vendor's
 * 04_RTC_Test example, which matches the PCF85063A datasheet.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Probe the chip. Returns false (and logs) if it does not answer — a
 * missing backup battery or an empty socket is not fatal: the HMI keeps
 * running with an unsynced clock and shows the "Time?" warning. */
bool rtc_init(void);

/* Current UTC epoch seconds, walking forward on millis() between reads.
 * Returns false when the RTC has never held a sane time. */
bool rtc_get_utc(uint32_t *epoch_out);

/* Write UTC epoch, verify by reading back. */
bool rtc_set_utc(uint32_t epoch);

/* Last-read sanity (year >= 2024). */
bool rtc_valid(void);

/* Force a fresh register read (called hourly and after a set). */
void rtc_refresh(void);
