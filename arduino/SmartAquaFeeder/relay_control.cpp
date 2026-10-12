/**
 * relay_control.cpp — see relay_control.h.
 */

#include "relay_control.h"
#include "feeder_clock.h"
#include "feeder_config.h"
#include "feeder_store.h"
#include "saq_portion.h"

#include <Arduino.h>
#include <string.h>

/* Live feed state. Written only from loop() context, except the emergency
 * off path which touches the GPIO alone (see relay_emergency_off). */
typedef struct {
    bool running;     /* energised or finishing up */
    bool pending_log; /* de-energised, log entry still to write */
    uint16_t portion_g;
    uint16_t run_ms;      /* planned (clamped) duration */
    uint32_t start_ms;    /* millis() at energise */
    uint32_t start_epoch; /* UTC epoch at energise, 0 if unsynced */
    uint8_t trigger;
    bool was_clamped;
    bool emergency; /* stopped via relay_emergency_off */
} feed_state_t;

static feed_state_t s_feed = {0};

static void relay_pin_off(void) {
#if FEEDER_RELAY_ACTIVE_HIGH
    digitalWrite(FEEDER_RELAY_PIN, LOW);
#else
    digitalWrite(FEEDER_RELAY_PIN, HIGH);
#endif
}

static void relay_pin_on(void) {
#if FEEDER_RELAY_ACTIVE_HIGH
    digitalWrite(FEEDER_RELAY_PIN, HIGH);
#else
    digitalWrite(FEEDER_RELAY_PIN, LOW);
#endif
}

void relay_init(void) {
    pinMode(FEEDER_RELAY_PIN, OUTPUT);
    relay_pin_off(); /* the very first thing: de-energised, always */
    s_feed.running = false;
    s_feed.pending_log = false;
}

uint32_t relay_clamp_for_test(uint32_t absurd_ms) {
    if (absurd_ms > SAQ_MAX_FEED_MS) {
        return SAQ_MAX_FEED_MS;
    }
    if (absurd_ms < SAQ_MIN_FEED_MS) {
        return SAQ_MIN_FEED_MS;
    }
    return absurd_ms;
}

relay_start_result_t relay_start(uint16_t ms, uint16_t portion_g, uint8_t trigger) {
    if (s_feed.running || s_feed.pending_log) {
        return RELAY_START_BUSY;
    }

    /* ---- the hard clamp: below the command layer, not configurable ---- */
    uint32_t clamped = (ms == 0) ? SAQ_MIN_FEED_MS : (uint32_t)ms;
    if (clamped > SAQ_MAX_FEED_MS) {
        clamped = SAQ_MAX_FEED_MS;
    }
    if (clamped < SAQ_MIN_FEED_MS) {
        clamped = SAQ_MIN_FEED_MS;
    }

    memset(&s_feed, 0, sizeof(s_feed));
    s_feed.portion_g = portion_g;
    s_feed.run_ms = (uint16_t)clamped;
    s_feed.trigger = trigger;
    s_feed.was_clamped = ((uint32_t)ms > clamped);
    s_feed.start_ms = millis();
    s_feed.start_epoch = clock_epoch();
    s_feed.running = true;

    /* Crash-safety record BEFORE energising (M2). */
    feeder_inprog_t rec;
    rec.magic = FEEDER_INPROG_MAGIC;
    rec.trigger = trigger;
    rec.portion_g = portion_g;
    rec.run_ms = s_feed.run_ms;
    rec.start_epoch = s_feed.start_epoch;
    rec.start_ms = s_feed.start_ms;
    store_set_inprog(&rec);

    relay_pin_on();
    return RELAY_START_OK;
}

void relay_emergency_off(void) {
    /* GPIO only — safe from the ESP-NOW receive callback. */
    relay_pin_off();
    if (s_feed.running) {
        s_feed.running = false;
        s_feed.emergency = true;
        s_feed.pending_log = true;
    }
}

static void relay_finalize(void) {
    if (!s_feed.pending_log) {
        return;
    }

    uint32_t actual_ms = millis() - s_feed.start_ms;
    if (actual_ms > 0xFFFF) {
        actual_ms = 0xFFFF;
    }

    feeder_log_entry_t e;
    memset(&e, 0, sizeof(e));
    e.event_epoch = s_feed.start_epoch;
    e.portion_g = s_feed.portion_g;
    e.actual_ms = (uint16_t)actual_ms;
    e.trigger = s_feed.trigger;
    if (s_feed.emergency) {
        e.result = SAQ_FEED_STOPPED;
    } else if (s_feed.was_clamped) {
        e.result = SAQ_FEED_CLAMPED;
    } else {
        e.result = SAQ_FEED_COMPLETED;
    }
    store_log_append(&e);
    store_clear_inprog();

    /* Leave the rest of the state readable until the next start. */
    s_feed.pending_log = false;
}

void relay_tick(void) {
    if (s_feed.running) {
        if ((uint32_t)(millis() - s_feed.start_ms) >= (uint32_t)s_feed.run_ms) {
            relay_pin_off();
            s_feed.running = false;
            s_feed.pending_log = true;
        }
    }
    if (s_feed.pending_log) {
        relay_finalize();
    }
}

bool relay_active(void) {
    return s_feed.running || s_feed.pending_log;
}

uint32_t relay_elapsed_ms(void) {
    if (!s_feed.running) {
        return 0;
    }
    return (uint32_t)(millis() - s_feed.start_ms);
}

void relay_recover_interrupted(void) {
    feeder_inprog_t rec;
    if (!store_get_inprog(&rec)) {
        return;
    }
    /* We cannot know how long the auger actually ran before the reboot —
     * recording the planned duration would be a lie. Record 0 and the
     * interrupted result; that is the honest version. */
    feeder_log_entry_t e;
    memset(&e, 0, sizeof(e));
    e.event_epoch = rec.start_epoch;
    e.portion_g = rec.portion_g;
    e.actual_ms = 0;
    e.trigger = rec.trigger;
    e.result = SAQ_FEED_INTERRUPTED;
    store_log_append(&e);
    store_clear_inprog();

    Serial.printf("[FEEDER] recovered interrupted feed: %u g, result=interrupted\n", (unsigned)rec.portion_g);
}
