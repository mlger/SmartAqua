/**
 * saq_schedule.cpp — see saq_schedule.h.
 */

#include "saq_schedule.h"

#include <string.h>
#include <stdio.h>

uint8_t saq_wday_of(uint32_t local_epoch_s)
{
    return (uint8_t)(((local_epoch_s / 86400u) + SAQ_EPOCH_WDAY_OFFSET) % 7u);
}

uint8_t saq_hour_of(uint32_t local_epoch_s)
{
    return (uint8_t)((local_epoch_s % 86400u) / 3600u);
}

uint8_t saq_minute_of(uint32_t local_epoch_s)
{
    return (uint8_t)((local_epoch_s % 3600u) / 60u);
}

uint32_t saq_epoch_min_of(uint32_t local_epoch_s)
{
    return local_epoch_s / 60u;
}

uint32_t saq_day_index_of(uint32_t local_epoch_s)
{
    return local_epoch_s / 86400u;
}

bool saq_sched_entry_valid(const saq_sched_entry_t *e)
{
    if (e == NULL) {
        return false;
    }
    if ((e->flags & ~SAQ_SCHED_FLAG_ENABLED) != 0) {
        return false;
    }
    if (e->hour > 23 || e->minute > 59) {
        return false;
    }
    if ((e->days & ~SAQ_DAY_ALL) != 0 || e->days == 0) {
        return false;
    }
    if (e->portion_g < SAQ_PORTION_MIN_G || e->portion_g > SAQ_PORTION_MAX_G) {
        return false;
    }
    return true;
}

int saq_sched_find_due(const saq_sched_entry_t *entries, uint8_t count,
                       const uint32_t *last_fired_min, uint32_t local_epoch_s)
{
    if ((entries == NULL) || (last_fired_min == NULL)) {
        return -1;
    }
    const uint8_t wday = saq_wday_of(local_epoch_s);
    const uint8_t hour = saq_hour_of(local_epoch_s);
    const uint8_t minute = saq_minute_of(local_epoch_s);
    const uint32_t cur_min = saq_epoch_min_of(local_epoch_s);

    for (uint8_t i = 0; i < count && i < SAQ_SCHED_MAX; i++) {
        const saq_sched_entry_t *e = &entries[i];
        if ((e->flags & SAQ_SCHED_FLAG_ENABLED) == 0) {
            continue;
        }
        if ((e->days & (uint8_t)(1u << wday)) == 0) {
            continue;
        }
        if ((e->hour != hour) || (e->minute != minute)) {
            continue;
        }
        if (last_fired_min[i] == cur_min) {
            continue;   /* already fired this minute — the double-feed guard */
        }
        return (int)i;
    }
    return -1;
}

bool saq_sched_next_due(const saq_sched_entry_t *entries, uint8_t count,
                        uint32_t local_epoch_s,
                        uint32_t *out_epoch_s, uint8_t *out_index)
{
    if ((entries == NULL) || (count == 0)) {
        return false;
    }

    /* Start from the next whole minute strictly after now. */
    uint32_t base_min = local_epoch_s / 60u + 1u;

    for (uint32_t step = 0; step < 8u * 1440u; step++) {
        const uint32_t m = base_min + step;                     /* candidate epoch-minute */
        const uint32_t secs = m * 60u;
        const uint8_t wday = saq_wday_of(secs);
        const uint8_t hh = (uint8_t)((secs % 86400u) / 3600u);
        const uint8_t mm = (uint8_t)((secs % 3600u) / 60u);
        for (uint8_t i = 0; i < count && i < SAQ_SCHED_MAX; i++) {
            const saq_sched_entry_t *e = &entries[i];
            if ((e->flags & SAQ_SCHED_FLAG_ENABLED) == 0) {
                continue;
            }
            if ((e->days & (uint8_t)(1u << wday)) == 0) {
                continue;
            }
            if ((e->hour == hh) && (e->minute == mm)) {
                if (out_epoch_s != NULL) {
                    *out_epoch_s = secs;
                }
                if (out_index != NULL) {
                    *out_index = i;
                }
                return true;
            }
        }
    }
    return false;
}

void saq_sched_days_summary(uint8_t days, char *buf, size_t cap)
{
    if ((buf == NULL) || (cap == 0)) {
        return;
    }
    if (days == SAQ_DAY_ALL) {
        snprintf(buf, cap, "Every day");
        return;
    }
    if (days == (SAQ_DAY_MON | SAQ_DAY_TUE | SAQ_DAY_WED | SAQ_DAY_THU | SAQ_DAY_FRI)) {
        snprintf(buf, cap, "Weekdays");
        return;
    }
    if (days == (SAQ_DAY_SAT | SAQ_DAY_SUN)) {
        snprintf(buf, cap, "Weekends");
        return;
    }

    static const char names[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    size_t used = 0;
    buf[0] = '\0';
    int listed = 0;
    for (int d = 0; d < 7; d++) {
        if (days & (1u << d)) {
            int n = snprintf(buf + used, cap - used, "%s%s", (listed ? " " : ""), names[d]);
            if (n < 0) {
                break;
            }
            used += (size_t)n;
            if (used >= cap - 1) {
                break;
            }
            listed++;
        }
    }
    if (listed == 1) {
        /* single day reads nicer as "Sundays" style is overkill; keep short */
    }
    if (listed == 0) {
        snprintf(buf, cap, "No days");
    }
}

void saq_sched_time_str(const saq_sched_entry_t *e, char *buf, size_t cap)
{
    if ((buf == NULL) || (cap < 6) || (e == NULL)) {
        return;
    }
    snprintf(buf, cap, "%02u:%02u", (unsigned)e->hour, (unsigned)e->minute);
}
