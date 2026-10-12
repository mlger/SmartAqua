/**
 * ui_settings.cpp — see ui_settings.h.
 *
 * Sections per screens.md: Link, Time, Feeding, Device, About.
 *
 * Two flows worth reading before changing:
 *   - Pairing: arms beacon acceptance for 60 s ("hold the button on the
 *     feeder for 3 s"), then PAIR_REQ, ACK, key upgrade — orchestrated
 *     here, executed by saq_link.
 *   - Calibration: a guided flow, not a raw number field — run a 20 s test
 *     feed, weigh what came out, let the firmware do the division.
 */

#include "ui_settings.h"
#include "SmartAquaHMI_version.h"
#include "app_model.h"
#include "saq_link.h"
#include "saq_portion.h"
#include "saq_rtc.h"
#include "saq_schedule.h"
#include "ui_chrome.h"
#include "ui_diagnostics.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_lbl_pair_state;
static lv_obj_t *s_lbl_pair_mac;
static lv_obj_t *s_btn_pair;
static lv_obj_t *s_lbl_pair_btn; /* label inside the Pair button */

static lv_obj_t *s_lbl_time_state;
static lv_obj_t *s_tz_roller;

/* time-set modal */
static lv_obj_t *s_ts_modal;
static lv_obj_t *s_ts_year, *s_ts_mon, *s_ts_day, *s_ts_hour, *s_ts_min;

/* calibration flow */
typedef enum { CAL_IDLE, CAL_RUNNING, CAL_WEIGH } cal_state_t;
static cal_state_t s_cal_state = CAL_IDLE;
static lv_obj_t *s_lbl_cal_state;
static lv_obj_t *s_btn_cal;
static lv_obj_t *s_cal_modal;
static lv_obj_t *s_cal_grams;

#define SETTINGS_CARD_W (800 - 2 * SAQ_SP_LG)
#define SETTINGS_CARD_H 96
#define SETTINGS_X      SAQ_SP_LG

static lv_obj_t *make_section(lv_obj_t *parent, const char *heading, int y) {
    lv_obj_t *card = ui_make_card(parent, SETTINGS_CARD_W, LV_SIZE_CONTENT);
    /* fixed height instead: sections are tall enough as drawn */
    lv_obj_set_size(card, SETTINGS_CARD_W, SETTINGS_CARD_H);
    lv_obj_set_pos(card, SETTINGS_X, y);
    lv_obj_t *h = ui_make_label(card, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(h, heading);
    return card;
}

/* ------------------------------------------------------------------ */
/* Link section                                                        */
/* ------------------------------------------------------------------ */

static void on_pair_arm(void);

static void on_repair_confirm(bool yes, void *user) {
    (void)user;
    if (yes) {
        link_unpair();
        on_pair_arm();
    }
}

static void on_pair_click(lv_event_t *e) {
    (void)e;
    if (g_app.paired) {
        ui_confirm("Re-pair", "Replace the stored feeder with a new pairing?", "Re-pair", "Cancel", false,
                   on_repair_confirm, NULL);
        return;
    }
    on_pair_arm();
}

static void on_pair_arm(void) {
    link_set_pair_armed(true);
    lv_label_set_text(s_lbl_pair_state, "Pairing… hold the feeder button 3 s");
    lv_obj_add_state(s_btn_pair, LV_STATE_DISABLED);
}

void ui_settings_on_beacon(const uint8_t mac[6], uint8_t fw_major, uint8_t fw_minor) {
    g_app.feeder_fw_major = fw_major;
    g_app.feeder_fw_minor = fw_minor;
    if (!link_pair_armed()) {
        return;
    }
    /* Auto-claim the first beacon that arrives while armed — the feeder's
     * window only opens when someone is standing at the tank pressing its
     * button, which is the physical-gesture half of the auth. */
    char buf[64];
    snprintf(buf, sizeof(buf), "Found feeder (fw %u.%u) — pairing…", (unsigned)fw_major, (unsigned)fw_minor);
    lv_label_set_text(s_lbl_pair_state, buf);
    (void)link_pair_to(mac);
}

void ui_settings_on_paired(void) {
    s_cal_state = CAL_IDLE;
    ui_settings_refresh();
    ui_toast("Paired with feeder", UI_TOAST_OK);
}

/* ------------------------------------------------------------------ */
/* Time section                                                        */
/* ------------------------------------------------------------------ */

static void make_options(char *buf, size_t cap, int from, int to, const char *fmt) {
    buf[0] = '\0';
    for (int i = from; i <= to; i++) {
        char item[8];
        snprintf(item, sizeof(item), fmt, i);
        strncat(buf, (i == from) ? item : "\n", cap - strlen(buf) - 1);
        if (i != from) {
            strncat(buf, item, cap - strlen(buf) - 1);
        }
    }
}

static void ts_close(void) {
    if (s_ts_modal != NULL) {
        lv_obj_del(s_ts_modal);
        s_ts_modal = NULL;
    }
}

static void on_ts_save(lv_event_t *e) {
    (void)e;
    const int y = 2020 + (int)lv_roller_get_selected(s_ts_year);
    const int mo = 1 + (int)lv_roller_get_selected(s_ts_mon);
    const int d = 1 + (int)lv_roller_get_selected(s_ts_day);
    const int h = (int)lv_roller_get_selected(s_ts_hour);
    const int mi = (int)lv_roller_get_selected(s_ts_min);

    /* local -> UTC via the roller's tz (already applied to the model) */
    const int32_t tz_s = model_tz_s();
    int64_t days = 0;
    {
        int yy = y;
        unsigned mm = (unsigned)mo, dd = (unsigned)d;
        yy -= (mm <= 2);
        const int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
        const unsigned yoe = (unsigned)(yy - era * 400);
        const unsigned doy = (153u * (mm + (mm > 2 ? -3u : 9u)) + 2u) / 5u + dd - 1u;
        const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
        days = era * 146097 + (int64_t)doe - 719468;
    }
    const int64_t local = days * 86400 + h * 3600 + mi * 60;
    const uint32_t utc = (uint32_t)(local - tz_s);

    if (!rtc_set_utc(utc)) {
        ui_toast("RTC write failed", UI_TOAST_DANGER);
    } else {
        g_app.clock_ok = true;
        ui_toast("Time set", UI_TOAST_OK);
        if (g_app.paired) {
            (void)link_send_set_time(utc, tz_s);
        }
    }
    ts_close();
}

static void on_ts_cancel(lv_event_t *e) {
    (void)e;
    ts_close();
}

static void on_set_time_click(lv_event_t *e) {
    (void)e;
    if (s_ts_modal != NULL) {
        return;
    }

    /* modal on the top layer: scrim + panel with 5 rollers */
    s_ts_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_ts_modal);
    lv_obj_set_size(s_ts_modal, 800, 480);
    lv_obj_set_style_bg_color(s_ts_modal, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ts_modal, LV_OPA_60, LV_PART_MAIN);

    lv_obj_t *panel = lv_obj_create(s_ts_modal);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 640, 300);
    lv_obj_set_style_bg_color(panel, SAQ_C_SURFACE_RAISED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, SAQ_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, SAQ_R_MD, LV_PART_MAIN);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = ui_make_label(panel, SAQ_FONT_HEADING, SAQ_C_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, SAQ_SP_SM);
    lv_label_set_text(t, "Set date and time");

    char opts[160];
    uint32_t loc = 0;
    if (model_local_epoch() != 0) {
        loc = model_local_epoch();
    }
    const uint32_t sod = loc % 86400u;
    const uint32_t day_idx = loc / 86400u;

    make_options(opts, sizeof(opts), 0, 9, "%d");
    s_ts_year = lv_roller_create(panel);
    lv_roller_set_options(s_ts_year, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_ts_year, 3);
    lv_obj_set_size(s_ts_year, 110, 110);
    lv_obj_set_pos(s_ts_year, 40, 56);

    make_options(opts, sizeof(opts), 1, 12, "%d");
    s_ts_mon = lv_roller_create(panel);
    lv_roller_set_options(s_ts_mon, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_ts_mon, 3);
    lv_obj_set_size(s_ts_mon, 100, 110);
    lv_obj_set_pos(s_ts_mon, 170, 56);

    make_options(opts, sizeof(opts), 1, 31, "%d");
    s_ts_day = lv_roller_create(panel);
    lv_roller_set_options(s_ts_day, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_ts_day, 3);
    lv_obj_set_size(s_ts_day, 100, 110);
    lv_obj_set_pos(s_ts_day, 290, 56);

    make_options(opts, sizeof(opts), 0, 23, "%02d");
    s_ts_hour = lv_roller_create(panel);
    lv_roller_set_options(s_ts_hour, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_ts_hour, 3);
    lv_obj_set_size(s_ts_hour, 100, 110);
    lv_obj_set_pos(s_ts_hour, 410, 56);

    make_options(opts, sizeof(opts), 0, 59, "%02d");
    s_ts_min = lv_roller_create(panel);
    lv_roller_set_options(s_ts_min, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_ts_min, 3);
    lv_obj_set_size(s_ts_min, 100, 110);
    lv_obj_set_pos(s_ts_min, 530, 56);

    /* preselect current values when the clock is sane */
    if (loc != 0) {
        lv_roller_set_selected(s_ts_year, (int16_t)(day_idx / 365u), LV_ANIM_OFF);
        lv_roller_set_selected(s_ts_mon, 0, LV_ANIM_OFF);
        lv_roller_set_selected(s_ts_day, 0, LV_ANIM_OFF);
        lv_roller_set_selected(s_ts_hour, (int16_t)(sod / 3600u), LV_ANIM_OFF);
        lv_roller_set_selected(s_ts_min, (int16_t)((sod % 3600u) / 60u), LV_ANIM_OFF);
    }

    lv_obj_t *btn_cancel = ui_make_btn(panel, UI_BTN_GHOST);
    lv_obj_set_size(btn_cancel, 168, SAQ_BTN_H);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, SAQ_SP_LG, -SAQ_SP_MD);
    lv_obj_add_event_cb(btn_cancel, on_ts_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = ui_make_label(btn_cancel, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_center(cl);
    lv_label_set_text(cl, "Cancel");

    lv_obj_t *btn_save = ui_make_btn(panel, UI_BTN_PRIMARY);
    lv_obj_set_size(btn_save, 168, SAQ_BTN_H);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -SAQ_SP_LG, -SAQ_SP_MD);
    lv_obj_add_event_cb(btn_save, on_ts_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = ui_make_label(btn_save, SAQ_FONT_BODY, SAQ_C_ON_PRIMARY);
    lv_obj_center(sl);
    lv_label_set_text(sl, "Set time");
}

static void on_tz_changed(lv_event_t *e) {
    (void)e;
    const int32_t tz = (int32_t)lv_roller_get_selected(s_tz_roller) - 12;
    model_set_tz(tz);
    if (g_app.paired && g_app.clock_ok) {
        (void)link_send_set_time(model_epoch(), model_tz_s());
    }
    ui_settings_refresh();
}

/* ------------------------------------------------------------------ */
/* Calibration flow                                                    */
/* ------------------------------------------------------------------ */

static void cal_close_modal(void) {
    if (s_cal_modal != NULL) {
        lv_obj_del(s_cal_modal);
        s_cal_modal = NULL;
    }
}

static void on_cal_weigh_save(lv_event_t *e) {
    (void)e;
    /* roller holds whole grams, 1..40 */
    const int grams = 1 + (int)lv_roller_get_selected(s_cal_grams);
    if (grams < 1) {
        return;
    }
    /* 20 s test feed delivered `grams` grams -> ms per gram */
    uint32_t ms_per_g = 20000u / (uint32_t)grams;
    uint16_t ms16 = (ms_per_g > 0xFFFF) ? 0xFFFF : (uint16_t)ms_per_g;
    if (ms16 < 100) {
        ms16 = 100; /* sanity floor (feeder NACKs below this) */
    }
    model_set_cfg(ms16, g_app.default_portion_g);
    if (g_app.paired) {
        (void)link_send_set_config(ms16, g_app.default_portion_g);
    }
    s_cal_state = CAL_IDLE;
    cal_close_modal();
    ui_settings_refresh();
    char buf[48];
    snprintf(buf, sizeof(buf), "Calibrated: %u ms/g", (unsigned)ms16);
    ui_toast(buf, UI_TOAST_OK);
}

static void on_cal_weigh_cancel(lv_event_t *e) {
    (void)e;
    s_cal_state = CAL_IDLE;
    cal_close_modal();
    ui_settings_refresh();
}

void ui_settings_on_cal_feed_done(uint16_t portion_g, uint16_t actual_ms, uint8_t result) {
    (void)portion_g;
    if (s_cal_state != CAL_RUNNING) {
        return;
    }
    if (result != SAQ_FEED_COMPLETED && result != SAQ_FEED_CLAMPED) {
        s_cal_state = CAL_IDLE;
        ui_settings_refresh();
        ui_toast("Test feed failed", UI_TOAST_DANGER);
        return;
    }
    s_cal_state = CAL_WEIGH;
    lv_label_set_text(s_lbl_cal_state, "Weigh the food that came out");

    /* weigh modal */
    s_cal_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_cal_modal);
    lv_obj_set_size(s_cal_modal, 800, 480);
    lv_obj_set_style_bg_color(s_cal_modal, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_cal_modal, LV_OPA_60, LV_PART_MAIN);

    lv_obj_t *panel = lv_obj_create(s_cal_modal);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 560, 300);
    lv_obj_set_style_bg_color(panel, SAQ_C_SURFACE_RAISED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, SAQ_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, SAQ_R_MD, LV_PART_MAIN);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = ui_make_label(panel, SAQ_FONT_HEADING, SAQ_C_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, SAQ_SP_SM);
    lv_label_set_text(t, "Calibrate");

    lv_obj_t *b = ui_make_label(panel, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, SAQ_SP_XL + SAQ_SP_SM);
    lv_label_set_text(b, "How many grams came out in 20 seconds?");

    char opts[400];
    make_options(opts, sizeof(opts), 1, 40, "%d");
    s_cal_grams = lv_roller_create(panel);
    lv_roller_set_options(s_cal_grams, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_cal_grams, 3);
    lv_obj_set_size(s_cal_grams, 140, 120);
    lv_obj_align(s_cal_grams, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_t *gl = ui_make_label(panel, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_align_to(gl, s_cal_grams, LV_ALIGN_OUT_RIGHT_MID, SAQ_SP_SM, 0);
    lv_label_set_text(gl, "grams");

    lv_obj_t *btn_cancel = ui_make_btn(panel, UI_BTN_GHOST);
    lv_obj_set_size(btn_cancel, 168, SAQ_BTN_H);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, SAQ_SP_LG, -SAQ_SP_MD);
    lv_obj_add_event_cb(btn_cancel, on_cal_weigh_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl2 = ui_make_label(btn_cancel, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_center(cl2);
    lv_label_set_text(cl2, "Cancel");

    lv_obj_t *btn_save = ui_make_btn(panel, UI_BTN_PRIMARY);
    lv_obj_set_size(btn_save, 168, SAQ_BTN_H);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -SAQ_SP_LG, -SAQ_SP_MD);
    lv_obj_add_event_cb(btn_save, on_cal_weigh_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl2 = ui_make_label(btn_save, SAQ_FONT_BODY, SAQ_C_ON_PRIMARY);
    lv_obj_center(sl2);
    lv_label_set_text(sl2, "Save");
}

static void on_cal_click(lv_event_t *e) {
    (void)e;
    if (s_cal_state != CAL_IDLE) {
        return;
    }
    if (!g_app.paired || !g_app.link_up) {
        ui_toast("Feeder must be online", UI_TOAST_WARN);
        return;
    }
    /* forced_ms = 20000, portion 0: the calibration test feed */
    if (!link_send_feed(0, 20000)) {
        ui_toast("Could not send test feed", UI_TOAST_DANGER);
        return;
    }
    s_cal_state = CAL_RUNNING;
    lv_label_set_text(s_lbl_cal_state, "Running 20 s test feed…");
    lv_obj_add_state(s_btn_cal, LV_STATE_DISABLED);
}

/* ------------------------------------------------------------------ */
/* Build + refresh                                                     */
/* ------------------------------------------------------------------ */

void ui_settings_create(lv_obj_t *parent) {
    /* This tab is taller than the content area — it scrolls. */
    lv_obj_add_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);

    /* ---- Link ---- */
    lv_obj_t *card_link = make_section(parent, "LINK", SAQ_SP_LG);
    s_lbl_pair_state = ui_make_label(card_link, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_align(s_lbl_pair_state, LV_ALIGN_TOP_LEFT, 0, SAQ_SP_MD + SAQ_SP_XS);
    s_lbl_pair_mac = ui_make_label(card_link, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_obj_align(s_lbl_pair_mac, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    s_btn_pair = ui_make_btn(card_link, UI_BTN_PRIMARY);
    lv_obj_set_size(s_btn_pair, 180, SAQ_BTN_H - SAQ_SP_SM);
    lv_obj_align(s_btn_pair, LV_ALIGN_RIGHT_MID, -SAQ_SP_SM, 0);
    lv_obj_add_event_cb(s_btn_pair, on_pair_click, LV_EVENT_CLICKED, NULL);
    s_lbl_pair_btn = ui_make_label(s_btn_pair, SAQ_FONT_BODY, SAQ_C_ON_PRIMARY);
    lv_obj_center(s_lbl_pair_btn);
    lv_label_set_text(s_lbl_pair_btn, "Pair");

    /* ---- Time ---- */
    lv_obj_t *card_time = make_section(parent, "TIME", SAQ_SP_LG + SETTINGS_CARD_H + SAQ_SP_MD);
    s_lbl_time_state = ui_make_label(card_time, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_align(s_lbl_time_state, LV_ALIGN_TOP_LEFT, 0, SAQ_SP_MD + SAQ_SP_XS);

    lv_obj_t *tz_lbl = ui_make_label(card_time, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_obj_align(tz_lbl, LV_ALIGN_BOTTOM_LEFT, 0, -SAQ_SP_SM);
    lv_label_set_text(tz_lbl, "Timezone (UTC offset)");

    char tz_opts[320];
    tz_opts[0] = '\0';
    for (int h = -12; h <= 14; h++) {
        char item[10];
        snprintf(item, sizeof(item), "%s%+d", (h == -12) ? "" : "\n", h);
        strncat(tz_opts, item, sizeof(tz_opts) - strlen(tz_opts) - 1);
    }
    s_tz_roller = lv_roller_create(card_time);
    lv_roller_set_options(s_tz_roller, tz_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_tz_roller, 2);
    lv_obj_set_size(s_tz_roller, 120, 64);
    lv_obj_align(s_tz_roller, LV_ALIGN_BOTTOM_LEFT, 220, -SAQ_SP_XS);
    lv_obj_add_event_cb(s_tz_roller, on_tz_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_roller_set_selected(s_tz_roller, (int16_t)(g_app.tz_h + 12), LV_ANIM_OFF);

    lv_obj_t *btn_ts = ui_make_btn(card_time, UI_BTN_PRIMARY);
    lv_obj_set_size(btn_ts, 180, SAQ_BTN_H - SAQ_SP_SM);
    lv_obj_align(btn_ts, LV_ALIGN_RIGHT_MID, -SAQ_SP_SM, 0);
    lv_obj_add_event_cb(btn_ts, on_set_time_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *tl = ui_make_label(btn_ts, SAQ_FONT_BODY, SAQ_C_ON_PRIMARY);
    lv_obj_center(tl);
    lv_label_set_text(tl, "Set time");

    /* ---- Feeding ---- */
    lv_obj_t *card_feed = make_section(parent, "FEEDING", SAQ_SP_LG + 2 * (SETTINGS_CARD_H + SAQ_SP_MD));
    s_lbl_cal_state = ui_make_label(card_feed, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_align(s_lbl_cal_state, LV_ALIGN_TOP_LEFT, 0, SAQ_SP_MD + SAQ_SP_XS);
    s_btn_cal = ui_make_btn(card_feed, UI_BTN_PRIMARY);
    lv_obj_set_size(s_btn_cal, 180, SAQ_BTN_H - SAQ_SP_SM);
    lv_obj_align(s_btn_cal, LV_ALIGN_RIGHT_MID, -SAQ_SP_SM, 0);
    lv_obj_add_event_cb(s_btn_cal, on_cal_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cal_l = ui_make_label(s_btn_cal, SAQ_FONT_BODY, SAQ_C_ON_PRIMARY);
    lv_obj_center(cal_l);
    lv_label_set_text(cal_l, "Calibrate");

    /* ---- Device ---- */
    lv_obj_t *card_dev = make_section(parent, "DEVICE", SAQ_SP_LG + 3 * (SETTINGS_CARD_H + SAQ_SP_MD));
    char dbuf[96];
    snprintf(dbuf, sizeof(dbuf), "HMI fw %d.%d  -  feeder fw %u.%u  -  protocol v%u.%u", HMI_FW_MAJOR, HMI_FW_MINOR,
             (unsigned)g_app.feeder_fw_major, (unsigned)g_app.feeder_fw_minor, (unsigned)SAQ_PROTO_VERSION_MAJOR,
             (unsigned)SAQ_PROTO_VERSION_MINOR);
    lv_obj_t *dl = ui_make_label(card_dev, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_align(dl, LV_ALIGN_TOP_LEFT, 0, SAQ_SP_MD + SAQ_SP_XS);
    lv_label_set_text(dl, dbuf);

    lv_obj_t *btn_diag = ui_make_btn(card_dev, UI_BTN_GHOST);
    lv_obj_set_size(btn_diag, 180, SAQ_BTN_H - SAQ_SP_SM);
    lv_obj_align(btn_diag, LV_ALIGN_RIGHT_MID, -SAQ_SP_SM, 0);
    lv_obj_add_event_cb(
        btn_diag,
        [](lv_event_t *ev) {
            (void)ev;
            ui_show_overlay(ui_diagnostics_get());
        },
        LV_EVENT_CLICKED, NULL);
    lv_obj_t *dgl = ui_make_label(btn_diag, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_center(dgl);
    lv_label_set_text(dgl, "Diagnostics");

    /* ---- About ---- */
    lv_obj_t *card_about = make_section(parent, "ABOUT", SAQ_SP_LG + 4 * (SETTINGS_CARD_H + SAQ_SP_MD));
    lv_obj_t *al = ui_make_label(card_about, SAQ_FONT_BODY, SAQ_C_TEXT_MUTED);
    lv_obj_align(al, LV_ALIGN_TOP_LEFT, 0, SAQ_SP_MD + SAQ_SP_XS);
    lv_label_set_text(al, "SmartAqua — github.com/mlger/SmartAqua");

    ui_settings_refresh();
}

void ui_settings_refresh(void) {
    if (g_app.paired) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Paired - feeder fw %u.%u", (unsigned)g_app.feeder_fw_major,
                 (unsigned)g_app.feeder_fw_minor);
        lv_label_set_text(s_lbl_pair_state, buf);
        snprintf(buf, sizeof(buf), "peer …%02X%02X", g_app.peer_mac[4],
                 g_app.peer_mac[5]); /* last 4 hex, not the whole thing */
        lv_label_set_text(s_lbl_pair_mac, buf);
        lv_label_set_text(s_lbl_pair_btn, "Re-pair");
        lv_obj_clear_state(s_btn_pair, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(s_lbl_pair_state, "Not paired");
        lv_label_set_text(s_lbl_pair_mac, "");
        lv_label_set_text(s_lbl_pair_btn, "Pair");
    }

    char tbuf[64];
    if (g_app.clock_ok) {
        const uint32_t loc = model_local_epoch();
        snprintf(tbuf, sizeof(tbuf), "Time source: RTC battery-backed, UTC%+d", (int)g_app.tz_h);
    } else {
        snprintf(tbuf, sizeof(tbuf), "Clock not set — unsynced warning is showing");
    }
    lv_label_set_text(s_lbl_time_state, tbuf);

    switch (s_cal_state) {
    case CAL_RUNNING:
        lv_label_set_text(s_lbl_cal_state, "Running 20 s test feed…");
        break;
    case CAL_WEIGH:
        lv_label_set_text(s_lbl_cal_state, "Weigh the food that came out");
        break;
    default: {
        char cbuf[64];
        snprintf(cbuf, sizeof(cbuf), "Calibration: %u ms/g (default portion %u g)", (unsigned)g_app.ms_per_g,
                 (unsigned)g_app.default_portion_g);
        lv_label_set_text(s_lbl_cal_state, cbuf);
        lv_obj_clear_state(s_btn_cal, LV_STATE_DISABLED);
        break;
    }
    }
}
