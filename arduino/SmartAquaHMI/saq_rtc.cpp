/**
 * saq_rtc.cpp — see saq_rtc.h.
 */

#include "saq_rtc.h"

#include <Arduino.h>
#include <driver/i2c.h>
#include <esp_log.h>
#include <string.h>

static const char *TAG = "saq_rtc";

#define RTC_I2C_PORT     I2C_NUM_0     /* shared with touch + expander */
#define RTC_I2C_ADDR     0x51
#define RTC_I2C_TIMEOUT  pdMS_TO_TICKS(50)

/* PCF85063A registers (04_RTC_Test / datasheet) */
#define RTC_REG_CTRL_1   0x00
#define RTC_REG_CTRL_2   0x01
#define RTC_REG_SECONDS  0x04
#define RTC_REG_MINUTES  0x05
#define RTC_REG_HOURS    0x06
#define RTC_REG_DAYS     0x07
#define RTC_REG_WEEKDAYS 0x08
#define RTC_REG_MONTHS   0x09
#define RTC_REG_YEARS    0x0A

#define RTC_CTRL_1_STOP  0x20

#define RTC_YEAR_BASE    1970u
#define RTC_YEAR_MIN     2024u     /* sanity floor: an unset chip reads 2000 */

static bool     s_present = false;
static bool     s_valid = false;
static uint32_t s_epoch_base = 0;
static uint32_t s_ms_base = 0;

/* ---- BCD + calendar helpers ----------------------------------------- */

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10u + (v & 0x0Fu)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
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
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    *d = doy - (153u * mp + 2u) / 5u + 1u;
    *m = mp + (mp < 10u ? 3u : -9u);
    *y = (int)(yy + (*m <= 2));
}

/* ---- raw register access (legacy IDF driver, port 0) ---------------- */

static bool rtc_write_regs(uint8_t first, const uint8_t *data, size_t n)
{
    uint8_t buf[8];
    if (n > 7) {
        return false;
    }
    buf[0] = first;
    memcpy(buf + 1, data, n);
    return i2c_master_write_to_device(RTC_I2C_PORT, RTC_I2C_ADDR, buf, n + 1,
                                      RTC_I2C_TIMEOUT) == ESP_OK;
}

static bool rtc_read_regs(uint8_t first, uint8_t *data, size_t n)
{
    return i2c_master_write_read_device(RTC_I2C_PORT, RTC_I2C_ADDR, &first, 1,
                                        data, n, RTC_I2C_TIMEOUT) == ESP_OK;
}

/* ---- public API ------------------------------------------------------ */

bool rtc_init(void)
{
    uint8_t secs = 0;
    s_present = rtc_read_regs(RTC_REG_SECONDS, &secs, 1);
    if (!s_present) {
        ESP_LOGW(TAG, "PCF85063A not answering at 0x51 — clock unsynced");
        return false;
    }
    rtc_refresh();
    return true;
}

void rtc_refresh(void)
{
    uint8_t regs[7];   /* seconds..years */
    if (!rtc_read_regs(RTC_REG_SECONDS, regs, 7)) {
        s_present = false;
        return;
    }
    s_present = true;

    const uint8_t sec = bcd2bin(regs[0] & 0x7Fu);
    const uint8_t min = bcd2bin(regs[1] & 0x7Fu);
    uint8_t hour = regs[2] & 0x3Fu;
    hour = bcd2bin(hour);
    const uint8_t day = bcd2bin(regs[3] & 0x3Fu);
    const uint8_t mon = bcd2bin(regs[4] & 0x1Fu);
    const uint16_t year = RTC_YEAR_BASE + bcd2bin(regs[5] & 0xFFu);

    if ((sec > 59) || (min > 59) || (hour > 23) || (day < 1) || (day > 31) ||
        (mon < 1) || (mon > 12) || (year < RTC_YEAR_MIN)) {
        s_valid = false;      /* never set, or battery died */
        return;
    }
    const int64_t days = days_from_civil((int)year, mon, day);
    s_epoch_base = (uint32_t)(days * 86400 + hour * 3600 + min * 60 + sec);
    s_ms_base = millis();
    s_valid = true;
}

bool rtc_valid(void)
{
    return s_valid;
}

bool rtc_get_utc(uint32_t *epoch_out)
{
    if (!s_valid) {
        return false;
    }
    if (epoch_out != NULL) {
        *epoch_out = s_epoch_base + (uint32_t)((millis() - s_ms_base) / 1000u);
    }
    return true;
}

bool rtc_set_utc(uint32_t epoch)
{
    if (!s_present) {
        return false;
    }
    int64_t days = (int64_t)epoch / 86400;
    const uint32_t sod = epoch % 86400u;
    int y;
    unsigned mo, d;
    civil_from_days(days, &y, &mo, &d);
    if ((y < (int)RTC_YEAR_BASE) || (y > (int)(RTC_YEAR_BASE + 99))) {
        return false;        /* the year register only holds 00-99 */
    }

    uint8_t regs[7];
    regs[0] = bin2bcd((uint8_t)(sod % 60u));
    regs[1] = bin2bcd((uint8_t)((sod % 3600u) / 60u));
    regs[2] = bin2bcd((uint8_t)(sod / 3600u));
    regs[3] = bin2bcd((uint8_t)d);
    regs[4] = bin2bcd((uint8_t)mo);
    regs[5] = bin2bcd((uint8_t)(y - (int)RTC_YEAR_BASE));
    regs[6] = 0;             /* weekdays register — not used */

    /* Stop the clock, write, restart: the datasheet's recommended order
     * so a tick mid-write cannot corrupt the time. */
    uint8_t ctrl1 = RTC_CTRL_1_STOP;
    if (!rtc_write_regs(RTC_REG_CTRL_1, &ctrl1, 1)) {
        return false;
    }
    if (!rtc_write_regs(RTC_REG_SECONDS, regs, 7)) {
        ctrl1 = 0;
        (void)rtc_write_regs(RTC_REG_CTRL_1, &ctrl1, 1);
        return false;
    }
    ctrl1 = 0;
    if (!rtc_write_regs(RTC_REG_CTRL_1, &ctrl1, 1)) {
        return false;
    }

    rtc_refresh();
    return s_valid;
}
