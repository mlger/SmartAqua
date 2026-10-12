/*
 * Host test runner for SmartAquaCore — compiles the library exactly as it
 * ships in the Arduino build and runs its self-test on the host, no board
 * attached. This is the Arduino-world equivalent of the planned host tests
 * in components/ (docs/01 § "Where things live in code").
 *
 * Build & run: scripts/run_host_tests.sh — exit code = number of failures.
 */
#include "saq_portion.h"
#include "saq_protocol.h"
#include "saq_schedule.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void host_check(const char *name, bool ok) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok) {
        failures++;
    }
}

int main(void) {
    char report[4096];
    int proto_failures = saq_protocol_self_test(report, sizeof(report));
    printf("%s\n", report);
    host_check("protocol self-test vectors", proto_failures == 0);

    /* ---- schedule matching ------------------------------------------ */
    saq_sched_entry_t entries[2] = {0};
    entries[0].hour = 8;
    entries[0].minute = 0;
    entries[0].days = SAQ_DAY_ALL;
    entries[0].flags = SAQ_SCHED_FLAG_ENABLED;
    entries[0].portion_g = 3;
    entries[1].hour = 18;
    entries[1].minute = 30;
    entries[1].days = SAQ_DAY_MON;
    entries[1].flags = SAQ_SCHED_FLAG_ENABLED;
    entries[1].portion_g = 2;

    uint32_t last_fired[2] = {0};

    /* days-since-epoch to 2026-01-05 (a Monday) = 20493 */
    const uint32_t monday_0800 = 20493u * 86400u + 8u * 3600u;
    host_check("wday of 2026-01-05 is Monday (1)", saq_wday_of(monday_0800) == 1);
    host_check("entry 0 due Monday 08:00", saq_sched_find_due(entries, 2, last_fired, monday_0800) == 0);

    last_fired[0] = monday_0800 / 60u;
    host_check("entry 0 not due twice in the same minute",
               saq_sched_find_due(entries, 2, last_fired, monday_0800) == -1);

    const uint32_t monday_1830 = monday_0800 + 10u * 3600u + 30u * 60u;
    host_check("entry 1 (Mon 18:30) not due at 08:00",
               saq_sched_find_due(entries, 2, last_fired, monday_1830 - (10u * 3600u + 30u * 60u)) == -1);
    host_check("entry 1 due Monday 18:30", saq_sched_find_due(entries, 2, last_fired, monday_1830) == 1);

    /* Tuesday: entry 1 has no Tuesday bit */
    const uint32_t tuesday_1830 = monday_1830 + 86400u;
    host_check("entry 1 not due Tuesday (days mask)", saq_sched_find_due(entries, 2, last_fired, tuesday_1830) == -1);

    /* ---- next feed --------------------------------------------------- */
    uint32_t next_epoch = 0;
    uint8_t next_idx = 0;
    host_check("next feed from Sunday 22:00 is Monday 08:00",
               saq_sched_next_due(entries, 2, monday_0800 - 6u * 3600u - 60u, &next_epoch, &next_idx) &&
                   (next_idx == 0) && (saq_hour_of(next_epoch) == 8) && (saq_minute_of(next_epoch) == 0));

    /* ---- validation -------------------------------------------------- */
    saq_sched_entry_t bad = entries[0];
    bad.days = 0;
    host_check("entry with no days is invalid", !saq_sched_entry_valid(&bad));
    bad = entries[0];
    bad.hour = 24;
    host_check("entry with hour 24 is invalid", !saq_sched_entry_valid(&bad));
    bad = entries[0];
    bad.portion_g = 11;
    host_check("entry with 11 g portion is invalid", !saq_sched_entry_valid(&bad));
    host_check("entry 0 as built is valid", saq_sched_entry_valid(&entries[0]));

    /* ---- portion math ------------------------------------------------ */
    host_check("3 g at 6000 ms/g = 18000 ms", saq_portion_ms(3, 6000) == 18000u);
    host_check("10 g at 6000 ms/g clamps to 30000 ms", saq_portion_ms(10, 6000) == SAQ_MAX_FEED_MS);
    host_check("clamp warning fires for 10 g at 6000 ms/g", saq_portion_will_clamp(10, 6000));
    host_check("no clamp warning for 3 g at 6000 ms/g", !saq_portion_will_clamp(3, 6000));
    host_check("clamped grams for 10 g at 6000 ms/g = 5", saq_portion_clamped_g(10, 6000) == 5);
    host_check("zero calibration falls back to default, no div-by-zero", saq_portion_ms(3, 0) == 18000u);

    /* ---- day summaries ----------------------------------------------- */
    char buf[32];
    saq_sched_days_summary(SAQ_DAY_ALL, buf, sizeof(buf));
    host_check("all days summary", strcmp(buf, "Every day") == 0);
    saq_sched_days_summary(SAQ_DAY_MON | SAQ_DAY_WED | SAQ_DAY_FRI, buf, sizeof(buf));
    host_check("MWF summary", strcmp(buf, "Mon Wed Fri") == 0);
    saq_sched_days_summary(SAQ_DAY_SAT | SAQ_DAY_SUN, buf, sizeof(buf));
    host_check("weekend summary", strcmp(buf, "Weekends") == 0);

    printf("host test: %d failure(s)\n", failures);
    return failures;
}
