/**
 * ui_history.cpp — see ui_history.h.
 *
 * Rows are not tappable: this is a record, not a control surface. The `!`
 * marker on non-completed feeds is the point of the screen — it is how a
 * user learns a feed did not finish.
 */

#include "ui_history.h"
#include "ui_theme.h"
#include "ui_chrome.h"
#include "app_model.h"
#include "saq_schedule.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_overlay;
static lv_obj_t *s_list;
static lv_obj_t *s_lbl_empty;
static lv_obj_t *s_banner_stale;

static const char *trigger_name(uint8_t t)
{
    switch (t) {
    case SAQ_TRIGGER_SCHEDULED:    return "scheduled";
    case SAQ_TRIGGER_MANUAL_HMI:   return "manual";
    case SAQ_TRIGGER_LOCAL_BUTTON: return "button";
    default:                       return "?";
    }
}

static const char *result_name(uint8_t r)
{
    switch (r) {
    case SAQ_FEED_COMPLETED:  return "Fed";
    case SAQ_FEED_CLAMPED:    return "Fed (limited)";
    case SAQ_FEED_STOPPED:    return "Stopped";
    case SAQ_FEED_INTERRUPTED: return "Interrupted";
    default:                  return "?";
    }
}

void ui_history_create(void)
{
    s_overlay = ui_overlay_create("History");

    s_banner_stale = ui_make_card(s_overlay, 800 - 2 * SAQ_SP_LG, 44);
    lv_obj_set_pos(s_banner_stale, SAQ_SP_LG, 0);
    lv_obj_set_style_border_color(s_banner_stale, SAQ_C_WARN, LV_PART_MAIN);
    lv_obj_t *bl = ui_make_label(s_banner_stale, SAQ_FONT_CAPTION, SAQ_C_WARN);
    lv_obj_align(bl, LV_ALIGN_LEFT_MID, SAQ_SP_SM, 0);
    lv_label_set_text(bl, "Last updated more than 3 intervals ago — data may be stale");
    lv_obj_add_flag(s_banner_stale, LV_OBJ_FLAG_HIDDEN);

    s_lbl_empty = ui_make_label(s_overlay, SAQ_FONT_BODY, SAQ_C_TEXT_MUTED);
    lv_obj_align(s_lbl_empty, LV_ALIGN_TOP_MID, 0, 160);
    lv_label_set_text(s_lbl_empty, "Nothing fed yet");

    s_list = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_list);
    lv_obj_add_style(s_list, &saq_style_screen, LV_PART_MAIN);
    lv_obj_set_size(s_list, 800, 440 - 44);
    lv_obj_align(s_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, SAQ_SP_XS, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_top(s_list, SAQ_SP_MD, LV_PART_MAIN);
}

void ui_history_refresh(void)
{
    lv_obj_clean(s_list);

    if (g_app.hist_count == 0) {
        lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
    }

    if (g_app.paired && !g_app.link_up) {
        lv_obj_clear_flag(s_banner_stale, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_banner_stale, LV_OBJ_FLAG_HIDDEN);
    }

    uint32_t last_day = 0xFFFFFFFFu;
    for (uint16_t i = 0; i < g_app.hist_count; i++) {
        const saq_hist_entry_t *h = &g_app.hist[i];
        const uint32_t loc = h->event_epoch + (uint32_t)model_tz_s();
        const uint32_t day = loc / 86400u;

        if (day != last_day) {
            last_day = day;
            lv_obj_t *day_lbl = ui_make_label(s_list, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
            lv_label_set_text(day_lbl, (i == 0) ? "Today" : "Earlier");
        }

        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 800 - 2 * SAQ_SP_LG, SAQ_ROW_H);
        lv_obj_set_style_bg_color(row, SAQ_C_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, SAQ_C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(row, SAQ_R_MD, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        char tbuf[10];
        const uint32_t sod = loc % 86400u;
        snprintf(tbuf, sizeof(tbuf), "%02u:%02u",
                 (unsigned)(sod / 3600u), (unsigned)((sod % 3600u) / 60u));
        lv_obj_t *lt = ui_make_label(row, SAQ_FONT_BODY, SAQ_C_TEXT);
        lv_obj_align(lt, LV_ALIGN_LEFT_MID, SAQ_SP_MD, 0);
        lv_label_set_text(lt, tbuf);

        char mbuf[64];
        snprintf(mbuf, sizeof(mbuf), "%s %u g", result_name(h->result), (unsigned)h->portion_g);
        lv_obj_t *lm = ui_make_label(row, SAQ_FONT_BODY, SAQ_C_TEXT);
        lv_obj_align(lm, LV_ALIGN_LEFT_MID, 150, 0);
        lv_label_set_text(lm, mbuf);

        lv_obj_t *lg = ui_make_label(row, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
        lv_obj_align(lg, LV_ALIGN_RIGHT_MID, -SAQ_SP_LG - 60, 0);
        lv_label_set_text(lg, trigger_name(h->trigger));

        /* completion marker: check for done, warning triangle otherwise */
        lv_obj_t *mark = ui_make_label(row, SAQ_FONT_BODY,
                                       (h->result == SAQ_FEED_COMPLETED) ? SAQ_C_OK : SAQ_C_WARN);
        lv_obj_align(mark, LV_ALIGN_RIGHT_MID, -SAQ_SP_SM, 0);
        lv_label_set_text(mark, (h->result == SAQ_FEED_COMPLETED) ? LV_SYMBOL_OK : "!");
    }
}

lv_obj_t *ui_history_get(void)
{
    return s_overlay;
}
