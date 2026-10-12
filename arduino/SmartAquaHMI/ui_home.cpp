/**
 * ui_home.cpp — see ui_home.h and design/screens.md § Home.
 *
 * Answers "is everything fine?" in under two seconds, from across the
 * room — which is why the stale/offline states get the same design care
 * as the happy path.
 */

#include "ui_home.h"
#include "ui_theme.h"
#include "ui_chrome.h"
#include "app_model.h"
#include "saq_schedule.h"
#include "saq_portion.h"

#include <stdio.h>
#include <string.h>
#include <Arduino.h>

/* Overlay accessors implemented in ui_history.cpp / ui_diagnostics.cpp */
lv_obj_t *ui_history_get(void);
lv_obj_t *ui_diagnostics_get(void);

static lv_obj_t *s_card_next;      /* NEXT FEED */
static lv_obj_t *s_lbl_next_time;
static lv_obj_t *s_lbl_next_sub;
static lv_obj_t *s_card_hopper;    /* HOPPER */
static lv_obj_t *s_bar_hopper;
static lv_obj_t *s_lbl_hopper_pct;
static lv_obj_t *s_lbl_hopper_sub;
static lv_obj_t *s_card_last;      /* LAST FEED */
static lv_obj_t *s_lbl_last_time;
static lv_obj_t *s_lbl_last_sub;
static lv_obj_t *s_btn_feed_now;
static lv_obj_t *s_stale_chip;     /* "Offline hh:mm" chip */
static lv_obj_t *s_fault_banner;

/* ------------------------------------------------------------------ */
/* Card geometry: two columns, 368x156, 24 outer margin, 16 gutter      */
/* ------------------------------------------------------------------ */

#define HOME_CARD_W   368
#define HOME_CARD_H   156
#define HOME_X0       SAQ_SP_LG
#define HOME_Y0       SAQ_SP_LG
#define HOME_GUTTER   SAQ_SP_MD

static void on_card_click(lv_event_t *e)
{
    const int id = (int)(intptr_t)lv_event_get_user_data(e);
    switch (id) {
    case 0:  ui_nav_tab(UI_TAB_SCHED); break;    /* "Set a schedule" target */
    case 1:  ui_show_overlay(ui_diagnostics_get()); break;  /* hopper -> Diagnostics */
    case 2:  ui_show_overlay(ui_history_get()); break;      /* last feed -> History */
    default: break;
    }
}

static void on_feed_now(lv_event_t *e)
{
    (void)e;
    /* Never feeds from here — pushes the FeedNow flow (screens.md). */
    ui_nav_tab(UI_TAB_FEED);
}

static void on_home_timer(lv_timer_t *t)
{
    (void)t;
    ui_home_refresh();
}

/* ------------------------------------------------------------------ */
/* Content builders                                                    */
/* ------------------------------------------------------------------ */

static void fill_next_feed(void)
{
    if (!g_app.clock_ok) {
        /* Never derive a "next feed" from a clock that is not synced. */
        lv_label_set_text(s_lbl_next_time, "-");
        lv_label_set_text(s_lbl_next_sub, "Time not set");
        lv_obj_set_style_text_color(s_lbl_next_sub, SAQ_C_WARN, LV_PART_MAIN);
        return;
    }
    uint32_t next = 0;
    uint8_t idx = 0;
    if (!saq_sched_next_due(g_app.sched.entries, g_app.sched.count,
                            model_local_epoch(), &next, &idx)) {
        lv_label_set_text(s_lbl_next_time, "-");
        lv_label_set_text(s_lbl_next_sub, "None scheduled");
        lv_obj_set_style_text_color(s_lbl_next_sub, SAQ_C_TEXT_MUTED, LV_PART_MAIN);
        return;
    }
    char tbuf[8];
    const uint32_t sod = next % 86400u;
    snprintf(tbuf, sizeof(tbuf), "%02u:%02u",
             (unsigned)(sod / 3600u), (unsigned)((sod % 3600u) / 60u));
    lv_label_set_text(s_lbl_next_time, tbuf);

    uint32_t secs = 0;
    if (next > model_local_epoch()) {
        secs = next - model_local_epoch();
    }
    char sub[48];
    if (secs >= 86400u) {
        snprintf(sub, sizeof(sub), "in %u d %u h", (unsigned)(secs / 86400u),
                 (unsigned)((secs % 86400u) / 3600u));
    } else if (secs >= 3600u) {
        snprintf(sub, sizeof(sub), "in %u h %u m", (unsigned)(secs / 3600u),
                 (unsigned)((secs % 3600u) / 60u));
    } else {
        snprintf(sub, sizeof(sub), "in %u m", (unsigned)((secs + 59u) / 60u));
    }
    if (saq_portion_will_clamp(g_app.sched.entries[idx].portion_g, g_app.ms_per_g)) {
        snprintf(sub + strlen(sub), sizeof(sub) - strlen(sub), " (clamped)");
    }
    lv_label_set_text(s_lbl_next_sub, sub);
    lv_obj_set_style_text_color(s_lbl_next_sub, SAQ_C_TEXT_MUTED, LV_PART_MAIN);
}

static void fill_hopper(void)
{
    if (g_app.st_hopper_pct == SAQ_HOPPER_UNKNOWN) {
        /* 255 means no sensor — showing "0%" would train the user to
         * ignore the field (docs/07). */
        lv_bar_set_value(s_bar_hopper, 0, LV_ANIM_OFF);
        lv_label_set_text(s_lbl_hopper_pct, "No sensor");
        lv_label_set_text(s_lbl_hopper_sub, "Fit a level sensor to track food");
    } else {
        lv_bar_set_value(s_bar_hopper, g_app.st_hopper_pct, LV_ANIM_OFF);
        char buf[12];
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)g_app.st_hopper_pct);
        lv_label_set_text(s_lbl_hopper_pct, buf);
        lv_label_set_text(s_lbl_hopper_sub,
                          (g_app.st_hopper_pct < 20) ? "Low — refill soon" : "");
    }
}

static void fill_last_feed(void)
{
    if (g_app.hist_count == 0) {
        lv_label_set_text(s_lbl_last_time, "Never");
        lv_label_set_text(s_lbl_last_sub, "Nothing fed yet");
        return;
    }
    const saq_hist_entry_t *h = &g_app.hist[0];
    const uint32_t ev_local = h->event_epoch + (uint32_t)model_tz_s();
    const uint32_t now_local = model_local_epoch();
    const uint32_t sod = ev_local % 86400u;
    char tbuf[10];
    snprintf(tbuf, sizeof(tbuf), "%02u:%02u",
             (unsigned)(sod / 3600u), (unsigned)((sod % 3600u) / 60u));

    char when[24];
    if ((now_local >= ev_local) && ((now_local - ev_local) < 86400u) &&
        ((now_local / 86400u) == (ev_local / 86400u))) {
        snprintf(when, sizeof(when), "Today %s", tbuf);
    } else if ((now_local >= ev_local) && ((now_local - ev_local) < 2u * 86400u)) {
        snprintf(when, sizeof(when), "Yesterday %s", tbuf);
    } else {
        snprintf(when, sizeof(when), "%s", tbuf);
    }
    lv_label_set_text(s_lbl_last_time, when);

    const char *trig = "?";
    switch (h->trigger) {
    case SAQ_TRIGGER_SCHEDULED:    trig = "scheduled"; break;
    case SAQ_TRIGGER_MANUAL_HMI:   trig = "manual"; break;
    case SAQ_TRIGGER_LOCAL_BUTTON: trig = "button"; break;
    default: break;
    }
    char sub[64];
    if (h->result == SAQ_FEED_COMPLETED) {
        snprintf(sub, sizeof(sub), "%u g, %s", (unsigned)h->portion_g, trig);
    } else if (h->result == SAQ_FEED_CLAMPED) {
        snprintf(sub, sizeof(sub), "%u g, %s, limited", (unsigned)h->portion_g, trig);
    } else if (h->result == SAQ_FEED_STOPPED) {
        snprintf(sub, sizeof(sub), "%u g, %s, stopped", (unsigned)h->portion_g, trig);
    } else {
        snprintf(sub, sizeof(sub), "%u g, %s, interrupted", (unsigned)h->portion_g, trig);
    }
    lv_label_set_text(s_lbl_last_sub, sub);
    lv_obj_set_style_text_color(s_lbl_last_sub, (h->result == SAQ_FEED_COMPLETED) ? SAQ_C_TEXT_MUTED : SAQ_C_WARN, LV_PART_MAIN);
}

/* ------------------------------------------------------------------ */
/* Build + refresh                                                     */
/* ------------------------------------------------------------------ */

static lv_obj_t *make_stat_card(lv_obj_t *parent, const char *caption,
                                int x, int y, int click_id)
{
    lv_obj_t *card = ui_make_card(parent, HOME_CARD_W, HOME_CARD_H);
    lv_obj_set_pos(card, x, y);
    if (click_id >= 0) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_style(card, &saq_style_card_press, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(card, on_card_click, LV_EVENT_CLICKED, (void *)(intptr_t)click_id);
    }
    lv_obj_t *cap = ui_make_label(card, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_label_set_text(cap, caption);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

void ui_home_create(lv_obj_t *parent)
{
    /* ---- NEXT FEED ---- */
    s_card_next = make_stat_card(parent, "NEXT FEED", HOME_X0, HOME_Y0, 0);
    s_lbl_next_time = ui_make_label(s_card_next, SAQ_FONT_HERO, SAQ_C_TEXT);
    lv_obj_align(s_lbl_next_time, LV_ALIGN_TOP_LEFT, 0, SAQ_SP_MD + SAQ_SP_XS);
    s_lbl_next_sub = ui_make_label(s_card_next, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_obj_align(s_lbl_next_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_label_set_text(s_lbl_next_sub, "-");

    /* ---- HOPPER ---- */
    s_card_hopper = make_stat_card(parent, "HOPPER",
                                   HOME_X0 + HOME_CARD_W + HOME_GUTTER, HOME_Y0, 1);
    s_lbl_hopper_pct = ui_make_label(s_card_hopper, SAQ_FONT_HERO, SAQ_C_TEXT);
    lv_obj_align(s_lbl_hopper_pct, LV_ALIGN_TOP_RIGHT, 0, SAQ_SP_MD + SAQ_SP_XS);
    lv_label_set_text(s_lbl_hopper_pct, "No sensor");
    s_bar_hopper = lv_bar_create(s_card_hopper);
    lv_obj_set_size(s_bar_hopper, HOME_CARD_W - 2 * SAQ_SP_MD, 16);
    lv_obj_align(s_bar_hopper, LV_ALIGN_BOTTOM_LEFT, 0, -SAQ_SP_XL);
    lv_bar_set_range(s_bar_hopper, 0, 100);
    lv_obj_set_style_bg_color(s_bar_hopper, SAQ_C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_hopper, SAQ_C_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_hopper, SAQ_R_PILL, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_hopper, SAQ_R_PILL, LV_PART_INDICATOR);
    s_lbl_hopper_sub = ui_make_label(s_card_hopper, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_obj_align(s_lbl_hopper_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* ---- LAST FEED ---- */
    s_card_last = make_stat_card(parent, "LAST FEED",
                                 HOME_X0, HOME_Y0 + HOME_CARD_H + HOME_GUTTER, 2);
    s_lbl_last_time = ui_make_label(s_card_last, SAQ_FONT_HERO, SAQ_C_TEXT);
    lv_obj_align(s_lbl_last_time, LV_ALIGN_TOP_LEFT, 0, SAQ_SP_MD + SAQ_SP_XS);
    lv_label_set_text(s_lbl_last_time, "Never");
    s_lbl_last_sub = ui_make_label(s_card_last, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_obj_align(s_lbl_last_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* ---- FEED NOW (a push, never a direct feed) ---- */
    s_btn_feed_now = ui_make_btn(parent, UI_BTN_PRIMARY);
    lv_obj_set_size(s_btn_feed_now, HOME_CARD_W, HOME_CARD_H);
    lv_obj_set_pos(s_btn_feed_now, HOME_X0 + HOME_CARD_W + HOME_GUTTER,
                   HOME_Y0 + HOME_CARD_H + HOME_GUTTER);
    lv_obj_add_event_cb(s_btn_feed_now, on_feed_now, LV_EVENT_CLICKED, NULL);
    lv_obj_t *fl = ui_make_label(s_btn_feed_now, SAQ_FONT_HEADING, SAQ_C_ON_PRIMARY);
    lv_label_set_text(fl, "FEED NOW");
    lv_obj_center(fl);

    /* ---- stale chip ---- */
    s_stale_chip = lv_obj_create(parent);
    lv_obj_remove_style_all(s_stale_chip);
    lv_obj_set_size(s_stale_chip, 240, 32);
    lv_obj_align(s_stale_chip, LV_ALIGN_TOP_RIGHT, -SAQ_SP_LG, 0);
    lv_obj_set_style_bg_color(s_stale_chip, SAQ_C_SURFACE_RAISED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_stale_chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_stale_chip, SAQ_C_WARN, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_stale_chip, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_stale_chip, SAQ_R_SM, LV_PART_MAIN);
    lv_obj_add_flag(s_stale_chip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *sl = ui_make_label(s_stale_chip, SAQ_FONT_CAPTION, SAQ_C_WARN);
    lv_obj_center(sl);
    lv_label_set_text(sl, "stale");

    /* ---- fault banner (replaces nothing yet; sits on top of the grid) ---- */
    s_fault_banner = ui_make_card(parent, 800 - 2 * SAQ_SP_LG, 56);
    lv_obj_set_pos(s_fault_banner, SAQ_SP_LG, 0);
    lv_obj_set_style_bg_color(s_fault_banner, SAQ_C_DANGER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_fault_banner, SAQ_C_DANGER, LV_PART_MAIN);
    lv_obj_add_flag(s_fault_banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *fbl = ui_make_label(s_fault_banner, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_align(fbl, LV_ALIGN_LEFT_MID, SAQ_SP_SM, 0);
    lv_label_set_text(fbl, "Feeder fault — see Diagnostics");

    (void)lv_timer_create(on_home_timer, 30000, NULL);   /* "in x h" countdown */
    ui_home_refresh();
}

void ui_home_refresh(void)
{
    fill_next_feed();
    fill_hopper();
    fill_last_feed();

    if (g_app.paired && !g_app.link_up) {
        lv_obj_clear_flag(s_stale_chip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *l = lv_obj_get_child(s_stale_chip, 0);
        uint32_t age = 0;
        if (g_app.last_status_ms != 0) {
            age = (millis() - g_app.last_status_ms) / 1000u;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "Offline %u s", (unsigned)age);
        lv_label_set_text(l, buf);
    } else {
        lv_obj_add_flag(s_stale_chip, LV_OBJ_FLAG_HIDDEN);
    }
}
