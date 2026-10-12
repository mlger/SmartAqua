/**
 * ui_feednow.cpp — see ui_feednow.h and design/screens.md § FeedNow.
 *
 * There is no single-tap feed anywhere in this UI: the FEED button opens a
 * confirmation, the confirmation sends FEED and waits for the ACK, and the
 * screen reports only outcomes it has actually seen. A touchscreen that
 * claims success while the relay never moved is worse than an honest error.
 *
 * Note on the push-vs-tab deviation: screens.md draws FeedNow as a push
 * screen with no tab bar. In this firmware it lives as the "Feed" tab (the
 * chrome is shared), and during Running the tab bar locks — the same
 * "cannot wander off mid-task" guarantee, one implementation fewer.
 */

#include "ui_feednow.h"
#include "app_model.h"
#include "saq_link.h"
#include "saq_portion.h"
#include "ui_chrome.h"
#include "ui_theme.h"

#include <Arduino.h>
#include <stdio.h>

typedef enum {
    FN_IDLE = 0,
    FN_CONFIRMING,
    FN_SENDING,
    FN_RUNNING,
    FN_STOPPING,
} fn_state_t;

static fn_state_t s_state = FN_IDLE;
static uint16_t s_pending_g = 3;
static uint32_t s_run_start_ms = 0;

static lv_obj_t *s_lbl_portion;
static lv_obj_t *s_slider;
static lv_obj_t *s_btn_feed;
static lv_obj_t *s_lbl_feed_hint;
static lv_obj_t *s_spinner;
static lv_obj_t *s_lbl_elapsed;
static lv_obj_t *s_btn_stop;

/* ------------------------------------------------------------------ */

static void set_state(fn_state_t st) {
    s_state = st;

    const bool idle = (st == FN_IDLE);
    const bool running = (st == FN_RUNNING) || (st == FN_STOPPING);

    if (s_slider != NULL) {
        if (idle) {
            lv_obj_clear_state(s_slider, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_slider, LV_STATE_DISABLED);
        }
    }
    if (s_btn_feed != NULL) {
        if (idle) {
            lv_obj_clear_state(s_btn_feed, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_btn_feed, LV_STATE_DISABLED);
        }
    }
    if (s_lbl_feed_hint != NULL) {
        lv_label_set_text(s_lbl_feed_hint, (!g_app.paired)      ? "Not paired — pair in Settings"
                                           : (!g_app.link_up)   ? "Feeder offline"
                                           : (st == FN_SENDING) ? "Sending…"
                                                                : "");
    }
    if ((s_spinner != NULL) && (s_lbl_elapsed != NULL) && (s_btn_stop != NULL)) {
        if (running) {
            lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_lbl_elapsed, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_btn_stop, LV_OBJ_FLAG_HIDDEN);
            if (st == FN_STOPPING) {
                lv_obj_add_state(s_btn_stop, LV_STATE_DISABLED);
            } else {
                lv_obj_clear_state(s_btn_stop, LV_STATE_DISABLED);
            }
        } else {
            lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_lbl_elapsed, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_btn_stop, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Rule from screens.md: while running, the screen cannot be dismissed. */
    ui_tabbar_enable(!running);
}

static void portion_label_refresh(void) {
    const int32_t v = lv_slider_get_value(s_slider);
    s_pending_g = (uint16_t)v; /* grams x 2, kept for the record */
    char buf[24];
    snprintf(buf, sizeof(buf), "Portion: %u.%u g", (unsigned)(v / 2), (unsigned)((v % 2) * 5));
    lv_label_set_text(s_lbl_portion, buf);
}

static uint16_t pending_portion_g(void) {
    const int32_t v = lv_slider_get_value(s_slider);
    return (uint16_t)(v / 2);
}

/* ------------------------------------------------------------------ */
/* Confirmation -> send                                                */
/* ------------------------------------------------------------------ */

static void on_confirm_yes(bool yes, void *user) {
    (void)user;
    if (!yes) {
        return;
    }
    if (s_state != FN_IDLE) {
        return;
    }
    const uint16_t portion = pending_portion_g();
    if (!link_send_feed(portion, 0)) {
        ui_toast("Feeder not responding", UI_TOAST_DANGER);
        return;
    }
    set_state(FN_SENDING);
}

static void on_feed_btn(lv_event_t *e) {
    (void)e;
    if (s_state != FN_IDLE) {
        return;
    }
    char body[64];
    snprintf(body, sizeof(body), "Feed %u g now?", (unsigned)pending_portion_g());
    ui_confirm("Feed now", body, "Feed", "Cancel", false, on_confirm_yes, NULL);
}

static void on_stop_btn(lv_event_t *e) {
    (void)e;
    if (s_state == FN_RUNNING) {
        (void)link_send_stop();
        set_state(FN_STOPPING);
    }
}

/* ------------------------------------------------------------------ */
/* Elapsed timer — the real count, not just a spinner                  */
/* ------------------------------------------------------------------ */

static void on_run_timer(lv_timer_t *t) {
    (void)t;
    if ((s_state != FN_RUNNING) && (s_state != FN_STOPPING)) {
        return;
    }
    char buf[40];
    const uint32_t ms = millis() - s_run_start_ms;
    snprintf(buf, sizeof(buf), "%s… %u.%u s", (s_state == FN_STOPPING) ? "Stopping" : "Feeding", (unsigned)(ms / 1000u),
             (unsigned)((ms % 1000u) / 100u));
    lv_label_set_text(s_lbl_elapsed, buf);
}

/* ------------------------------------------------------------------ */
/* Public events                                                       */
/* ------------------------------------------------------------------ */

void ui_feednow_on_cmd_ack(bool ok, uint8_t nack_reason) {
    if (s_state != FN_SENDING) {
        return; /* not ours (time/config commands) */
    }
    if (ok) {
        s_run_start_ms = millis();
        set_state(FN_RUNNING);
        return;
    }
    const char *why = "Feeder refused";
    switch (nack_reason) {
    case SAQ_RES_REJECTED_DURATION:
        why = "Duration refused";
        break;
    case SAQ_RES_BUSY:
        why = "Feeder busy — try later";
        break;
    case SAQ_RES_BAD_PAYLOAD:
        why = "Feeder refused (bad request)";
        break;
    case SAQ_RES_NOT_PAIRED:
        why = "Not paired";
        break;
    default:
        break;
    }
    ui_toast(why, UI_TOAST_DANGER);
    set_state(FN_IDLE); /* no feed claimed */
}

void ui_feednow_on_feed_done(uint16_t portion_g, uint16_t actual_ms, uint8_t trigger, uint8_t result) {
    (void)trigger;
    if ((s_state != FN_RUNNING) && (s_state != FN_STOPPING)) {
        /* An unattended outcome (scheduled feed while the screen was
         * elsewhere) — surface it but do not reset this screen. */
        if (result == SAQ_FEED_COMPLETED) {
            char b[40];
            snprintf(b, sizeof(b), "Fed %u g", (unsigned)portion_g);
            ui_toast(b, UI_TOAST_OK);
        }
        return;
    }

    char buf[48];
    switch (result) {
    case SAQ_FEED_COMPLETED:
        snprintf(buf, sizeof(buf), "Fed %u g", (unsigned)portion_g);
        ui_toast(buf, UI_TOAST_OK);
        break;
    case SAQ_FEED_CLAMPED:
        snprintf(buf, sizeof(buf), "Limited to %u s", (unsigned)((actual_ms + 999u) / 1000u));
        ui_toast(buf, UI_TOAST_WARN);
        break;
    case SAQ_FEED_STOPPED:
        ui_toast("Stopped", UI_TOAST_WARN);
        break;
    default:
        ui_toast("Feed interrupted", UI_TOAST_DANGER);
        break;
    }
    set_state(FN_IDLE);
    ui_nav_tab(UI_TAB_HOME); /* pop back to Home (screens.md) */
}

void ui_feednow_on_link(bool up) {
    (void)up;
    if (s_state == FN_IDLE) {
        set_state(FN_IDLE); /* re-runs the hint text */
    }
}

/* ------------------------------------------------------------------ */
/* Build                                                               */
/* ------------------------------------------------------------------ */

void ui_feednow_create(lv_obj_t *parent) {
    s_lbl_portion = ui_make_label(parent, SAQ_FONT_HEADING, SAQ_C_TEXT);
    lv_obj_align(s_lbl_portion, LV_ALIGN_TOP_MID, 0, SAQ_SP_XL);
    lv_label_set_text(s_lbl_portion, "Portion: 3 g");

    s_slider = lv_slider_create(parent);
    lv_obj_set_width(s_slider, 560);
    lv_obj_align(s_slider, LV_ALIGN_TOP_MID, 0, SAQ_SP_XL + SAQ_SP_XL);
    lv_slider_set_range(s_slider, 2, 20); /* grams x 2 */
    lv_slider_set_value(s_slider, (int16_t)(g_app.default_portion_g * 2), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider, SAQ_C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider, SAQ_C_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, SAQ_C_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(
        s_slider,
        [](lv_event_t *e) {
            lv_obj_t *sl = lv_event_get_target(e);
            const int32_t v = lv_slider_get_value(sl);
            char buf[24];
            snprintf(buf, sizeof(buf), "Portion: %u.%u g", (unsigned)(v / 2), (unsigned)((v % 2) * 5));
            lv_label_set_text(s_lbl_portion, buf);
            s_pending_g = (uint16_t)v;
        },
        LV_EVENT_VALUE_CHANGED, NULL);

    /* 1 g / 5 g / 10 g scale marks (screens.md layout) */
    lv_obj_t *m1 = ui_make_label(parent, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_obj_align_to(m1, s_slider, LV_ALIGN_OUT_BOTTOM_LEFT, 0, SAQ_SP_XS);
    lv_label_set_text(m1, "1 g");
    lv_obj_t *m2 = ui_make_label(parent, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_obj_align_to(m2, s_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, SAQ_SP_XS);
    lv_label_set_text(m2, "5 g");
    lv_obj_t *m3 = ui_make_label(parent, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_obj_align_to(m3, s_slider, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, SAQ_SP_XS);
    lv_label_set_text(m3, "10 g");

    /* Clamp warning under the slider */
    s_lbl_feed_hint = ui_make_label(parent, SAQ_FONT_CAPTION, SAQ_C_WARN);
    lv_obj_align(s_lbl_feed_hint, LV_ALIGN_TOP_MID, 0, SAQ_SP_XL + SAQ_SP_XL + 72);
    lv_label_set_text(s_lbl_feed_hint, "");

    /* FEED button */
    s_btn_feed = ui_make_btn(parent, UI_BTN_PRIMARY);
    lv_obj_set_size(s_btn_feed, 420, SAQ_BTN_H);
    lv_obj_align(s_btn_feed, LV_ALIGN_BOTTOM_MID, 0, -SAQ_SP_XL);
    lv_obj_add_event_cb(s_btn_feed, on_feed_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *fl = ui_make_label(s_btn_feed, SAQ_FONT_HEADING, SAQ_C_ON_PRIMARY);
    lv_obj_center(fl);
    lv_label_set_text(fl, "FEED");

    /* Running widgets */
    s_spinner = lv_spinner_create(parent, 1000, 90);
    lv_obj_set_size(s_spinner, 48, 48);
    lv_obj_align(s_spinner, LV_ALIGN_BOTTOM_MID, -120, -SAQ_SP_XL);
    lv_obj_set_style_arc_color(s_spinner, SAQ_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, SAQ_C_PRIMARY, LV_PART_INDICATOR);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

    s_lbl_elapsed = ui_make_label(parent, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_align_to(s_lbl_elapsed, s_spinner, LV_ALIGN_OUT_RIGHT_MID, SAQ_SP_MD, 0);
    lv_label_set_text(s_lbl_elapsed, "");
    lv_obj_add_flag(s_lbl_elapsed, LV_OBJ_FLAG_HIDDEN);

    s_btn_stop = ui_make_btn(parent, UI_BTN_DANGER);
    lv_obj_set_size(s_btn_stop, 180, SAQ_BTN_H);
    lv_obj_align(s_btn_stop, LV_ALIGN_BOTTOM_RIGHT, -SAQ_SP_LG, -SAQ_SP_LG);
    lv_obj_add_event_cb(s_btn_stop, on_stop_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stl = ui_make_label(s_btn_stop, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_center(stl);
    lv_label_set_text(stl, "STOP");
    lv_obj_add_flag(s_btn_stop, LV_OBJ_FLAG_HIDDEN);

    (void)lv_timer_create(on_run_timer, 100, NULL);
    set_state(FN_IDLE);
    portion_label_refresh();
}
