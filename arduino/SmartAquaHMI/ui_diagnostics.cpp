/**
 * ui_diagnostics.cpp — see ui_diagnostics.h.
 *
 * Every field here earned its place by answering "which half is broken":
 * link counters separate offline from quiet-but-fine, feeder uptime
 * exposes reset loops, and the raw last-STATUS payload is the escape hatch
 * when everything else says "fine".
 */

#include "ui_diagnostics.h"
#include "ui_theme.h"
#include "ui_chrome.h"
#include "app_model.h"
#include "saq_link.h"
#include "saq_protocol.h"

#include <esp_system.h>
#include <esp_heap_caps.h>
#include <Arduino.h>

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_overlay;
static lv_obj_t *s_list;
static lv_timer_t *s_timer;

#define DIAG_ROWS 14
static lv_obj_t *s_rows[DIAG_ROWS];
static char s_row_buf[DIAG_ROWS][96];

static const char *DIAG_LABELS[DIAG_ROWS] = {
    "Link state",
    "Last ACK age",
    "TX frames / retries",
    "RX frames / dups / foreign",
    "Feeder uptime",
    "Feeder clock confidence",
    "Feeder fw / protocol",
    "HMI clock",
    "HMI uptime",
    "Free heap / min free",
    "Free PSRAM",
    "Last feed (feeder)",
    "Schedule count",
    "Last STATUS payload",
};

static void fill_rows(void)
{
    const bool up = g_app.paired && g_app.link_up;

    snprintf(s_row_buf[0], sizeof(s_row_buf[0]), "%s",
             !g_app.paired ? "not paired" : (up ? "up" : "down (3 intervals missed)"));
    snprintf(s_row_buf[1], sizeof(s_row_buf[1]), "%u ms",
             (unsigned)link_last_ack_age_ms());
    snprintf(s_row_buf[2], sizeof(s_row_buf[2]), "%u / %u",
             (unsigned)link_tx_frames(), (unsigned)link_tx_retries());
    snprintf(s_row_buf[3], sizeof(s_row_buf[3]), "%u / %u / %u",
             (unsigned)link_rx_frames(), (unsigned)link_rx_dups(),
             (unsigned)link_rx_foreign());
    snprintf(s_row_buf[4], sizeof(s_row_buf[4]), "%u s",
             (unsigned)g_app.st_uptime_s);
    snprintf(s_row_buf[5], sizeof(s_row_buf[5]), "%s",
             g_app.st_clock_conf ? "synced since boot" : "UNSYNCED — no scheduled feeds");
    snprintf(s_row_buf[6], sizeof(s_row_buf[6]), "%u.%u / v%u.%u",
             (unsigned)g_app.feeder_fw_major, (unsigned)g_app.feeder_fw_minor,
             (unsigned)SAQ_PROTO_VERSION_MAJOR, (unsigned)SAQ_PROTO_VERSION_MINOR);
    snprintf(s_row_buf[7], sizeof(s_row_buf[7]), "%s",
             g_app.clock_ok ? "RTC valid" : "RTC unset — push from Settings");
    snprintf(s_row_buf[8], sizeof(s_row_buf[8]), "%u s",
             (unsigned)(millis() / 1000u));
    snprintf(s_row_buf[9], sizeof(s_row_buf[9]), "%u B / %u B",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    snprintf(s_row_buf[10], sizeof(s_row_buf[10]), "%u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    if (g_app.st_last_feed_epoch == 0) {
        snprintf(s_row_buf[11], sizeof(s_row_buf[11]), "never");
    } else {
        const uint32_t loc = g_app.st_last_feed_epoch + (uint32_t)model_tz_s();
        snprintf(s_row_buf[11], sizeof(s_row_buf[11]), "%02u:%02u local",
                 (unsigned)((loc % 86400u) / 3600u),
                 (unsigned)((loc % 3600u) / 60u));
    }
    snprintf(s_row_buf[12], sizeof(s_row_buf[12]), "%u (feeder) / %u (cache)",
             (unsigned)g_app.st_sched_count, (unsigned)g_app.sched.count);
    snprintf(s_row_buf[13], sizeof(s_row_buf[13]),
             "clk=%u sched=%u log=%u up=%u%s",
             (unsigned)g_app.st_clock_conf, (unsigned)g_app.st_sched_count,
             (unsigned)g_app.st_last_log_seq, (unsigned)g_app.st_uptime_s,
             g_app.st_has_config ? " +cfg" : "");

    for (int i = 0; i < DIAG_ROWS; i++) {
        lv_label_set_text(s_rows[i], s_row_buf[i]);
    }
}

static void on_diag_timer(lv_timer_t *t)
{
    (void)t;
    if (ui_current_overlay() == s_overlay) {
        fill_rows();
    }
}

void ui_diagnostics_create(void)
{
    s_overlay = ui_overlay_create("Diagnostics");

    s_list = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_list);
    lv_obj_add_style(s_list, &saq_style_screen, LV_PART_MAIN);
    lv_obj_set_size(s_list, 800, 440);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, SAQ_SP_XS, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(s_list, SAQ_SP_LG, LV_PART_MAIN);

    for (int i = 0; i < DIAG_ROWS; i++) {
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 800 - 2 * SAQ_SP_LG, 44);
        lv_obj_set_style_bg_color(row, SAQ_C_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, SAQ_C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(row, SAQ_R_SM, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = ui_make_label(row, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, SAQ_SP_SM, 0);
        lv_obj_set_width(lbl, 280);
        lv_label_set_text(lbl, DIAG_LABELS[i]);

        s_rows[i] = ui_make_label(row, SAQ_FONT_SMALL, SAQ_C_TEXT);
        lv_obj_align(s_rows[i], LV_ALIGN_LEFT_MID, 300, 0);
        lv_obj_set_width(s_rows[i], 440);
        lv_label_set_long_mode(s_rows[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(s_rows[i], "-");
    }

    s_timer = lv_timer_create(on_diag_timer, 1000, NULL);
    (void)s_timer;
}

void ui_diagnostics_refresh(void)
{
    if (ui_current_overlay() == s_overlay) {
        fill_rows();
    }
}

lv_obj_t *ui_diagnostics_get(void)
{
    return s_overlay;
}
