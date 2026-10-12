/**
 * feeder_clock.cpp — see feeder_clock.h.
 *
 * The optional DS3231 is driven with ~60 lines of Wire code rather than a
 * library dependency: registers 0x00..0x06, BCD, 24-hour mode forced,
 * century bit handled. Keeping it dependency-free means the default build
 * needs only the Arduino core.
 */

#include "feeder_clock.h"
#include "feeder_store.h"
#include "feeder_config.h"

#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

static bool     s_synced = false;
static uint32_t s_epoch_base = 0;    /* UTC epoch at s_ms_base */
static uint32_t s_ms_base = 0;
static int32_t  s_tz_s = 0;
static uint32_t s_last_tick_s = 0;
#if FEEDER_HAVE_DS3231
static uint32_t s_last_rtc_read_ms = 0;
#endif

/* ---------- civil-calendar helpers (Howard Hinnant's algorithm) -------- */

static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = (int64_t)(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    *d = doy - (153u * mp + 2u) / 5u + 1u;
    *m = mp + (mp < 10u ? 3u : -9u);
    *y = (int)(yy + (*m <= 2));
}

/* ---------- optional DS3231 ------------------------------------------- */

#if FEEDER_HAVE_DS3231

#define DS3231_ADDR 0x68

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10u + (v & 0x0Fu)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

static bool ds3231_read(uint32_t *epoch_out)
{
    uint8_t regs[7];
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom((int)DS3231_ADDR, 7) != 7) {
        return false;
    }
    for (int i = 0; i < 7; i++) {
        regs[i] = (uint8_t)Wire.read();
    }
    const uint8_t sec = bcd2bin(regs[0]);
    const uint8_t min = bcd2bin(regs[1]);
    uint8_t hour = regs[2];
    if (hour & 0x40) {           /* 12-hour mode — normalise */
        hour = (uint8_t)((hour & 0x0F) + ((hour & 0x20) ? 12u : 0u));
    } else {
        hour = bcd2bin(hour);
    }
    const uint8_t date = bcd2bin(regs[3]);
    const uint8_t mon = bcd2bin((uint8_t)(regs[4] & 0x1F));
    const uint16_t year = (uint16_t)(2000u + bcd2bin(regs[5]));

    if ((sec > 59) || (min > 59) || (hour > 23) || (date < 1) || (date > 31) ||
        (mon < 1) || (mon > 12) || (year < 2020)) {
        return false;            /* not set / not sane */
    }
    int64_t days = days_from_civil(year, mon, date);
    *epoch_out = (uint32_t)(days * 86400 + hour * 3600 + min * 60 + sec);
    return true;
}

#endif /* FEEDER_HAVE_DS3231 */

/* ---------- public API ------------------------------------------------ */

void clock_init(void)
{
    s_tz_s = store_load_tz();
#if FEEDER_HAVE_DS3231
    Wire.begin(FEEDER_RTC_SDA, FEEDER_RTC_SCL);
    uint32_t rtc_epoch = 0;
    if (ds3231_read(&rtc_epoch)) {
        clock_set(rtc_epoch, s_tz_s);
        Serial.printf("[FEEDER] clock from DS3231: %lu\n", (unsigned long)rtc_epoch);
    } else {
        Serial.println("[FEEDER] DS3231 not set (or absent) — waiting for SET_TIME");
    }
#else
    Serial.println("[FEEDER] no RTC fitted — waiting for SET_TIME from the HMI");
#endif
}

void clock_set(uint32_t utc_epoch, int32_t tz_s)
{
    if (utc_epoch == 0) {
        return;                   /* epoch 0 means "unsynced" — never adopt it */
    }
    s_epoch_base = utc_epoch;
    s_ms_base = millis();
    s_tz_s = tz_s;                /* SET_TIME always carries the zone */
    s_synced = true;
    store_save_tz(tz_s);
}

bool clock_synced(void)
{
    return s_synced;
}

uint32_t clock_epoch(void)
{
    if (!s_synced) {
        return 0;
    }
    return s_epoch_base + (uint32_t)((millis() - s_ms_base) / 1000u);
}

uint32_t clock_local_epoch(void)
{
    if (!s_synced) {
        return 0;
    }
    return clock_epoch() + (uint32_t)s_tz_s;
}

int32_t clock_tz_s(void)
{
    return s_tz_s;
}

void clock_tick(void)
{
    const uint32_t now_s = (uint32_t)(millis() / 1000u);
    if (now_s == s_last_tick_s) {
        return;                   /* once per second is plenty */
    }
    s_last_tick_s = now_s;
#if FEEDER_HAVE_DS3231
    /* Hourly re-read: keeps millis() drift out of the schedule and
     * survives the (nonexistent on this chip) RTC jump. */
    if ((millis() - s_last_rtc_read_ms) > 3600000u) {
        s_last_rtc_read_ms = millis();
        uint32_t rtc_epoch = 0;
        if (ds3231_read(&rtc_epoch)) {
            int32_t tz = s_tz_s;
            s_epoch_base = rtc_epoch;
            s_ms_base = millis();
            s_tz_s = tz;
            s_synced = true;
        }
    }
#else
    /* nothing to re-read; millis() walks the clock forward */
#endif
}

void clock_str(char *buf, size_t cap)
{
    if ((buf == NULL) || (cap < 20)) {
        return;
    }
    if (!s_synced) {
        snprintf(buf, cap, "--:--:-- --/--/--");
        return;
    }
    const uint32_t loc = clock_local_epoch();
    const uint32_t sod = loc % 86400u;
    int y;
    unsigned mo, d;
    civil_from_days((int64_t)(loc / 86400u), &y, &mo, &d);
    snprintf(buf, cap, "%02u:%02u:%02u %02u/%02u/%04d",
             (unsigned)(sod / 3600u), (unsigned)((sod % 3600u) / 60u), (unsigned)(sod % 60u),
             d, mo, y);
}
