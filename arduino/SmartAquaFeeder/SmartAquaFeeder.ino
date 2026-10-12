/**
 * SmartAquaFeeder.ino — the feeder node firmware (classic ESP32).
 *
 * Owns: the relay, the auger motor, the schedule, the clock, the feed log.
 * Does not own: the touchscreen, the user, or anything that requires the
 * HMI to be alive (design rule 1: the feeder node is autonomous).
 *
 * Bring-up order follows docs/08-testing-and-bringup.md. In particular the
 * M2 bench checks this firmware is designed to pass:
 *   - the relay pin is driven to OFF as the first statement of setup()
 *   - the hard MAX_FEED_MS clamp lives in relay_control, below this layer
 *   - a crash mid-feed de-energises by hardware (NO contact) and is logged
 *     as "interrupted" on the next boot
 *
 * Serial console: 115200 baud. Prefix [FEEDER].
 */

#include <Arduino.h>

#include "feeder_config.h"
#include "feeder_store.h"
#include "feeder_clock.h"
#include "relay_control.h"
#include "saq_link.h"
#include "saq_portion.h"
#include "saq_schedule.h"
#include "saq_protocol.h"

/* ------------------------------------------------------------------ */
/* Scheduler                                                           */
/* ------------------------------------------------------------------ */

static uint32_t s_last_fired_min[SAQ_SCHED_MAX];
static uint32_t s_last_sched_check_s = 0xFFFFFFFFu;

static void scheduler_tick(void)
{
    const uint32_t now_s = millis() / 1000u;
    if (now_s == s_last_sched_check_s) {
        return;                    /* once per second */
    }
    s_last_sched_check_s = now_s;

    if (!clock_synced()) {
        return;                    /* rule 3: never feed on a clock you distrust */
    }
    if (relay_active()) {
        return;
    }

    const uint32_t local = clock_local_epoch();
    saq_sched_t sched;
    link_sched_snapshot(&sched);

    uint16_t ms_per_g = SAQ_MS_PER_G_DEFAULT, default_portion = 3;
    link_cfg(&ms_per_g, &default_portion);

    int due = -1;
    while ((due = saq_sched_find_due(sched.entries, sched.count,
                                     s_last_fired_min, local)) >= 0) {
        const saq_sched_entry_t *e = &sched.entries[due];
        s_last_fired_min[due] = saq_epoch_min_of(local);

        const uint16_t ms = (uint16_t)saq_portion_ms(e->portion_g, ms_per_g);

        Serial.printf("[FEEDER] scheduled feed: slot %d, %02u:%02u, %u g -> %u ms\n",
                      due, (unsigned)e->hour, (unsigned)e->minute,
                      (unsigned)e->portion_g, (unsigned)ms);

        const relay_start_result_t r = relay_start(ms, e->portion_g, SAQ_TRIGGER_SCHEDULED);
        if (r != RELAY_START_OK) {
            Serial.printf("[FEEDER] scheduled feed refused (%d)\n", (int)r);
        }
        /* one feed at a time; the next due entry waits for the next tick */
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Button + LED                                                        */
/* ------------------------------------------------------------------ */

static uint8_t  s_led_state = 0;   /* pattern id, see led_update() */
static bool     s_btn_down = false;
static uint32_t s_btn_down_ms = 0;
static bool     s_hold_fired = false;

static void button_tick(void)
{
    const bool down = (digitalRead(FEEDER_BUTTON_PIN) == LOW);

    if (down && !s_btn_down) {
        s_btn_down = true;
        s_btn_down_ms = millis();
        s_hold_fired = false;
    } else if (!down && s_btn_down) {
        s_btn_down = false;
        if (!s_hold_fired && (uint32_t)(millis() - s_btn_down_ms) < FEEDER_BUTTON_HOLD_MS) {
            /* short press: local manual feed with the default portion */
            uint16_t ms_per_g = SAQ_MS_PER_G_DEFAULT, portion = 3;
            link_cfg(&ms_per_g, &portion);
            const uint16_t ms = (uint16_t)saq_portion_ms(portion, ms_per_g);
            const relay_start_result_t r = relay_start(ms, portion, SAQ_TRIGGER_LOCAL_BUTTON);
            if (r == RELAY_START_OK) {
                Serial.printf("[FEEDER] local feed: %u g (%u ms)\n",
                              (unsigned)portion, (unsigned)ms);
            } else {
                Serial.println("[FEEDER] local feed refused — busy");
            }
        }
    } else if (down && s_btn_down && !s_hold_fired) {
        if ((uint32_t)(millis() - s_btn_down_ms) >= FEEDER_BUTTON_HOLD_MS) {
            s_hold_fired = true;
            link_open_pair_window();
        }
    }
}

/*
 * LED patterns — readable from across the room, which is the point:
 *   slow heartbeat  : paired, link healthy
 *   double blink    : booted, not paired
 *   fast blink      : pairing window open
 *   solid           : feeding
 *   triple blink    : clock unsynced (feeds will not fire)
 */
static void led_update(void)
{
    if (relay_active()) {
        digitalWrite(FEEDER_LED_PIN, HIGH);
        return;
    }
    if (s_led_state == 0) {
        digitalWrite(FEEDER_LED_PIN, HIGH);
    } else {
        digitalWrite(FEEDER_LED_PIN, LOW);
    }
}

static void led_tick(void)
{
    const uint32_t ms = millis() % 4000u;

    if (relay_active()) {
        s_led_state = 1;           /* solid, handled in led_update() */
    } else if (!clock_synced() && link_paired()) {
        /* triple blink every 4 s — the "something is wrong" pattern */
        s_led_state = (ms < 100) || ((ms >= 300) && (ms < 400)) || ((ms >= 600) && (ms < 700)) ? 1 : 0;
    } else if (link_pair_window_open()) {
        s_led_state = (ms % 250u) < 125u ? 1 : 0;
    } else if (!link_paired()) {
        s_led_state = (ms < 100) || ((ms >= 250) && (ms < 350)) ? 1 : 0;
    } else {
        s_led_state = (ms < 60) ? 1 : 0;   /* heartbeat */
    }
}

/* ------------------------------------------------------------------ */
/* Boot + loop                                                         */
/* ------------------------------------------------------------------ */

static void print_banner(void)
{
    Serial.println();
    Serial.println("=== SmartAqua feeder node ===");
    Serial.printf("fw %d.%d, protocol v%d.%d, node_id %u\n",
                  FEEDER_FW_MAJOR, FEEDER_FW_MINOR,
                  SAQ_PROTO_VERSION_MAJOR, SAQ_PROTO_VERSION_MINOR,
                  (unsigned)SAQ_NODE_ID);
    Serial.printf("relay on GPIO %d, button GPIO %d, LED GPIO %d, channel %d\n",
                  FEEDER_RELAY_PIN, FEEDER_BUTTON_PIN, FEEDER_LED_PIN,
                  SAQ_ESPNOW_CHANNEL);
    Serial.printf("MAX_FEED_MS = %u (hard clamp, not configurable)\n",
                  (unsigned)SAQ_MAX_FEED_MS);
}

void setup()
{
    /* The very first action, before anything can fail: relay off.
     * (M2: "relay silent during reset and boot".) */
    relay_init();

    Serial.begin(115200);
    delay(50);
    print_banner();

    store_init();
    clock_init();

    /* Protocol self-test: a codec regression surfaces here, on the
     * console, instead of as a mis-fed tank. */
    char report[2048];
    const int failures = saq_protocol_self_test(report, sizeof(report));
    Serial.printf("[FEEDER] protocol self-test: %d failure(s)\n", failures);
    if (failures > 0) {
        Serial.println(report);
    }

    relay_recover_interrupted();

    pinMode(FEEDER_BUTTON_PIN, INPUT_PULLUP);
    pinMode(FEEDER_LED_PIN, OUTPUT);
    digitalWrite(FEEDER_LED_PIN, LOW);

    for (uint8_t i = 0; i < SAQ_SCHED_MAX; i++) {
        s_last_fired_min[i] = 0;
    }

    link_init();

    /* Bench check from docs/08: prove the clamp with an absurd duration. */
    Serial.printf("[FEEDER] clamp test: 3600000 ms requested -> %u ms allowed\n",
                  (unsigned)relay_clamp_for_test(3600000u));

    char tbuf[24];
    clock_str(tbuf, sizeof(tbuf));
    Serial.printf("[FEEDER] clock: %s (synced=%d)\n", tbuf, (int)clock_synced());
    Serial.println("[FEEDER] ready.");
}

void loop()
{
    link_tick();
    relay_tick();
    scheduler_tick();
    button_tick();
    led_tick();
    led_update();
    delay(2);
}
