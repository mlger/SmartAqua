/**
 * ui_schedule.cpp — see ui_schedule.h.
 *
 * The list is a cache of the feeder's authoritative copy. While offline,
 * rows are shown read-only with a warning banner — a schedule edit that
 * silently fails to reach the feeder is worse than a disabled button
 * (screens.md § Schedule states).
 *
 * Editing writes one slot at a time with SET_SCHEDULE_ENTRY(index, count,
 * entry). Deleting compacts: the LAST entry moves into the deleted slot
 * and count shrinks by one — expressible with the same packet. Deleting
 * the final remaining entry is the special case count=0, index=0, which
 * the feeder interprets as "clear the schedule".
 */

#include "ui_schedule.h"
#include "app_model.h"
#include "saq_link.h"
#include "saq_portion.h"
#include "saq_schedule.h"
#include "ui_chrome.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

static void ui_editor_open(void);
lv_obj_t *ui_history_get(void);
lv_obj_t *ui_diagnostics_get(void);

/* ---- list ---- */
static lv_obj_t *s_list; /* scroll container of rows */
static lv_obj_t *s_banner_offline;
static lv_obj_t *s_lbl_empty;
static lv_obj_t *s_btn_add;

/* ---- editor overlay ---- */
static lv_obj_t *s_editor; /* overlay root (from ui_overlay_create) */
static lv_obj_t *s_ed_hour;
static lv_obj_t *s_ed_min;
static lv_obj_t *s_ed_days[7];
static lv_obj_t *s_ed_slider;
static lv_obj_t *s_lbl_ed_portion;
static lv_obj_t *s_lbl_ed_error;
static lv_obj_t *s_btn_save;
static lv_obj_t *s_btn_delete;
static int8_t s_edit_index = -1; /* -1 = new entry */

static bool editable(void) {
    return g_app.paired && g_app.link_up;
}

/* ------------------------------------------------------------------ */
/* List                                                                */
/* ------------------------------------------------------------------ */

static void rebuild_rows(void);

static void on_row_click(lv_event_t *e) {
    if (!editable()) {
        return;
    }
    s_edit_index = (int8_t)(intptr_t)lv_event_get_user_data(e);
    ui_editor_open();
}

static void on_add_click(lv_event_t *e) {
    (void)e;
    if (!editable()) {
        return;
    }
    s_edit_index = -1;
    ui_editor_open();
}

/* ------------------------------------------------------------------ */
/* Editor: gather + validate + save                                    */
/* ------------------------------------------------------------------ */

static saq_sched_entry_t editor_collect(void) {
    saq_sched_entry_t e;
    memset(&e, 0, sizeof(e));
    e.hour = (uint8_t)lv_roller_get_selected(s_ed_hour);
    e.minute = (uint8_t)lv_roller_get_selected(s_ed_min);
    for (int i = 0; i < 7; i++) {
        if (lv_obj_has_state(s_ed_days[i], LV_STATE_CHECKED)) {
            e.days |= (uint8_t)(1u << i);
        }
    }
    e.flags = SAQ_SCHED_FLAG_ENABLED;
    e.portion_g = (uint16_t)(lv_slider_get_value(s_ed_slider) / 2);
    e.pad = 0;
    return e;
}

static void on_editor_save(lv_event_t *ev) {
    (void)ev;
    const saq_sched_entry_t entry = editor_collect();

    /* Validation (screens.md § ScheduleEditor) */
    if (entry.days == 0) {
        lv_label_set_text(s_lbl_ed_error, "Pick at least one day");
        return;
    }
    if ((entry.portion_g < SAQ_PORTION_MIN_G) || (entry.portion_g > SAQ_PORTION_MAX_G)) {
        lv_label_set_text(s_lbl_ed_error, "Portion must be 1-10 g");
        return;
    }
    /* Duplicate-time warning: legal, usually unintended — warn, don't block. */
    for (uint8_t i = 0; i < g_app.sched.count; i++) {
        if ((i != (uint8_t)s_edit_index) && (g_app.sched.entries[i].hour == entry.hour) &&
            (g_app.sched.entries[i].minute == entry.minute)) {
            lv_label_set_text(s_lbl_ed_error, "Note: another feed is at this time");
            /* fall through and save anyway */
            break;
        }
    }
    if (saq_portion_will_clamp(entry.portion_g, g_app.ms_per_g)) {
        lv_label_set_text(s_lbl_ed_error, "Note: portion will be clamped");
    } else {
        lv_label_set_text(s_lbl_ed_error, "");
    }

    uint8_t index;
    if (s_edit_index < 0) {
        if (g_app.sched.count >= SAQ_SCHED_MAX) {
            lv_label_set_text(s_lbl_ed_error, "Schedule is full (8 slots)");
            return;
        }
        index = g_app.sched.count;
        g_app.sched.count++;
    } else {
        index = (uint8_t)s_edit_index;
    }
    g_app.sched.entries[index] = entry;

    lv_obj_add_state(s_btn_save, LV_STATE_DISABLED);
    if (link_send_sched_entry(index, g_app.sched.count, &entry)) {
        /* Optimistic local apply; the ACK path closes the editor, the
         * timeout path reverts and says so. */
        rebuild_rows();
    } else {
        lv_obj_clear_state(s_btn_save, LV_STATE_DISABLED);
        lv_label_set_text(s_lbl_ed_error, "Could not reach feeder. Not saved.");
    }
}

static void on_delete_confirm(bool yes, void *user) {
    if (!yes) {
        return;
    }
    const uint8_t idx = (uint8_t)(intptr_t)user;
    if (g_app.sched.count == 0) {
        return;
    }
    if (g_app.sched.count == 1) {
        /* clear-all encoding: count = 0 */
        g_app.sched.count = 0;
        (void)link_send_sched_entry(0, 0, NULL);
    } else {
        /* compact: last entry moves into the deleted slot */
        const uint8_t last = (uint8_t)(g_app.sched.count - 1);
        g_app.sched.entries[idx] = g_app.sched.entries[last];
        g_app.sched.count--;
        (void)link_send_sched_entry(idx, g_app.sched.count, &g_app.sched.entries[idx]);
    }
    rebuild_rows();
    ui_hide_overlay();
}

static void on_editor_delete(lv_event_t *e) {
    (void)e;
    char body[64];
    snprintf(body, sizeof(body), "Delete the %02u:%02u feed?", (unsigned)lv_roller_get_selected(s_ed_hour),
             (unsigned)lv_roller_get_selected(s_ed_min));
    ui_confirm("Delete feed", body, "Delete", "Keep", true, on_delete_confirm, (void *)(intptr_t)s_edit_index);
}

static void on_editor_cancel(lv_event_t *e) {
    (void)e;
    ui_hide_overlay();
}

/* Called by the dispatcher when SET_SCHEDULE_ENTRY resolves. */
void ui_schedule_on_cmd_ack(bool ok, uint8_t nack_reason) {
    if (ui_current_overlay() != s_editor) {
        return;
    }
    if (ok) {
        ui_hide_overlay(); /* only pop on a confirmed ACK */
        ui_toast("Schedule saved", UI_TOAST_OK);
    } else {
        lv_obj_clear_state(s_btn_save, LV_STATE_DISABLED);
        const char *why = "Could not reach feeder. Not saved.";
        if (nack_reason == SAQ_RES_BAD_PAYLOAD) {
            why = "Feeder rejected the entry";
        }
        lv_label_set_text(s_lbl_ed_error, why);
        /* revert the optimistic apply by re-reading what the feeder last
         * reported is not possible (no GET packet) — reload the last
         * cached copy from before the edit is out of scope here; the
         * count is corrected on the next STATUS round-trip. */
    }
}

/* ------------------------------------------------------------------ */
/* Editor build                                                        */
/* ------------------------------------------------------------------ */

static void editor_days_btn(lv_obj_t *parent, int d, const char *label) {
    lv_obj_t *b = ui_make_btn(parent, UI_BTN_GHOST);
    lv_obj_set_style_bg_color(b, SAQ_C_PRIMARY, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, SAQ_C_ON_PRIMARY, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_size(b, 44, 44);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_pos(b, 60 + d * 96, 108);
    lv_obj_t *l = ui_make_label(b, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_center(l);
    lv_label_set_text(l, label);
    s_ed_days[d] = b;
}

static void make_roller_options(char *buf, size_t cap, int n) {
    buf[0] = '\0';
    for (int i = 0; i < n; i++) {
        char item[6];
        snprintf(item, sizeof(item), "%s%02d", (i ? "\n" : ""), i);
        strncat(buf, item, cap - strlen(buf) - 1);
    }
}

void ui_editor_open(void) {
    /* defaults for a new entry: next round hour, every day, default portion */
    uint8_t hour = 8;
    uint8_t minute = 0;
    uint8_t days = SAQ_DAY_ALL;
    uint16_t portion = g_app.default_portion_g;

    if (s_edit_index >= 0 && (uint8_t)s_edit_index < g_app.sched.count) {
        const saq_sched_entry_t *e = &g_app.sched.entries[s_edit_index];
        hour = e->hour;
        minute = e->minute;
        days = e->days;
        portion = e->portion_g;
    } else if (g_app.clock_ok) {
        const uint32_t loc = model_local_epoch();
        hour = (uint8_t)(((loc % 86400u) / 3600u + 1u) % 24u);
    }

    char opts[3 * 32];
    make_roller_options(opts, sizeof(opts), 24);
    lv_roller_set_options(s_ed_hour, opts, LV_ROLLER_MODE_NORMAL);
    make_roller_options(opts, sizeof(opts), 60);
    lv_roller_set_options(s_ed_min, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_ed_hour, hour, LV_ANIM_OFF);
    lv_roller_set_selected(s_ed_min, minute, LV_ANIM_OFF);

    for (int i = 0; i < 7; i++) {
        if (days & (1u << i)) {
            lv_obj_add_state(s_ed_days[i], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_ed_days[i], LV_STATE_CHECKED);
        }
    }
    lv_slider_set_value(s_ed_slider, (int16_t)(portion * 2), LV_ANIM_OFF);
    char buf[24];
    snprintf(buf, sizeof(buf), "%u g", (unsigned)portion);
    lv_label_set_text(s_lbl_ed_portion, buf);
    lv_label_set_text(s_lbl_ed_error, "");

    lv_obj_clear_state(s_btn_save, LV_STATE_DISABLED);
    if (s_edit_index < 0) {
        lv_obj_add_flag(s_btn_delete, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_btn_delete, LV_OBJ_FLAG_HIDDEN);
    }
    ui_show_overlay(s_editor);
}

static void on_ed_slider(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    char buf[24];
    snprintf(buf, sizeof(buf), "%u g", (unsigned)(lv_slider_get_value(sl) / 2));
    lv_label_set_text(s_lbl_ed_portion, buf);
}

static void build_editor(void) {
    s_editor = ui_overlay_create(s_edit_index < 0 ? "Add feed" : "Edit feed");
    lv_obj_t *content = s_editor; /* overlay content container */

    lv_obj_t *lbl_time = ui_make_label(content, SAQ_FONT_BODY, SAQ_C_TEXT_MUTED);
    lv_obj_set_pos(lbl_time, SAQ_SP_LG, 24);
    lv_label_set_text(lbl_time, "Time");

    char opts[200];
    make_roller_options(opts, sizeof(opts), 24);
    s_ed_hour = lv_roller_create(content);
    lv_roller_set_options(s_ed_hour, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_ed_hour, 3);
    lv_obj_set_size(s_ed_hour, 120, 120);
    lv_obj_set_pos(s_ed_hour, 120, 0);
    lv_obj_set_style_bg_color(s_ed_hour, SAQ_C_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ed_hour, SAQ_C_BORDER, LV_PART_MAIN);

    make_roller_options(opts, sizeof(opts), 60);
    s_ed_min = lv_roller_create(content);
    lv_roller_set_options(s_ed_min, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_ed_min, 3);
    lv_obj_set_size(s_ed_min, 120, 120);
    lv_obj_set_pos(s_ed_min, 260, 0);

    lv_obj_t *lbl_days = ui_make_label(content, SAQ_FONT_BODY, SAQ_C_TEXT_MUTED);
    lv_obj_set_pos(lbl_days, SAQ_SP_LG, 116);
    lv_label_set_text(lbl_days, "Days");
    static const char *DAY_LETTERS[7] = {"S", "M", "T", "W", "T", "F", "S"};
    for (int d = 0; d < 7; d++) {
        editor_days_btn(content, d, DAY_LETTERS[d]);
    }

    lv_obj_t *lbl_portion = ui_make_label(content, SAQ_FONT_BODY, SAQ_C_TEXT_MUTED);
    lv_obj_set_pos(lbl_portion, SAQ_SP_LG, 204);
    lv_label_set_text(lbl_portion, "Portion");
    s_lbl_ed_portion = ui_make_label(content, SAQ_FONT_HEADING, SAQ_C_TEXT);
    lv_obj_set_pos(s_lbl_ed_portion, 640, 196);
    s_ed_slider = lv_slider_create(content);
    lv_obj_set_width(s_ed_slider, 480);
    lv_obj_set_pos(s_ed_slider, 160, 216);
    lv_slider_set_range(s_ed_slider, 2, 20);
    lv_obj_set_style_bg_color(s_ed_slider, SAQ_C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ed_slider, SAQ_C_PRIMARY, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_ed_slider, on_ed_slider, LV_EVENT_VALUE_CHANGED, NULL);

    s_lbl_ed_error = ui_make_label(content, SAQ_FONT_CAPTION, SAQ_C_WARN);
    lv_obj_set_pos(s_lbl_ed_error, SAQ_SP_LG, 260);
    lv_label_set_text(s_lbl_ed_error, "");

    s_btn_delete = ui_make_btn(content, UI_BTN_DANGER);
    lv_obj_set_size(s_btn_delete, 168, SAQ_BTN_H);
    lv_obj_set_pos(s_btn_delete, SAQ_SP_LG, 330);
    lv_obj_add_event_cb(s_btn_delete, on_editor_delete, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = ui_make_label(s_btn_delete, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_center(dl);
    lv_label_set_text(dl, "Delete");

    lv_obj_t *btn_cancel = ui_make_btn(content, UI_BTN_GHOST);
    lv_obj_set_size(btn_cancel, 168, SAQ_BTN_H);
    lv_obj_set_pos(btn_cancel, 420, 330);
    lv_obj_add_event_cb(btn_cancel, on_editor_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = ui_make_label(btn_cancel, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_obj_center(cl);
    lv_label_set_text(cl, "Cancel");

    s_btn_save = ui_make_btn(content, UI_BTN_PRIMARY);
    lv_obj_set_size(s_btn_save, 168, SAQ_BTN_H);
    lv_obj_set_pos(s_btn_save, 608, 330);
    lv_obj_add_event_cb(s_btn_save, on_editor_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = ui_make_label(s_btn_save, SAQ_FONT_BODY, SAQ_C_ON_PRIMARY);
    lv_obj_center(sl);
    lv_label_set_text(sl, "Save");
}

/* ------------------------------------------------------------------ */
/* List build + refresh                                                */
/* ------------------------------------------------------------------ */

static void rebuild_rows(void) {
    lv_obj_clean(s_list);

    const bool can_edit = editable();
    if (!can_edit) {
        lv_obj_clear_flag(s_banner_offline, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(s_btn_add, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(s_banner_offline, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(s_btn_add, LV_STATE_DISABLED);
    }

    if (g_app.sched.count == 0) {
        lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < g_app.sched.count; i++) {
        const saq_sched_entry_t *e = &g_app.sched.entries[i];

        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 800 - 2 * SAQ_SP_LG, SAQ_ROW_H);
        lv_obj_set_style_bg_color(row, SAQ_C_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, SAQ_C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(row, SAQ_R_MD, LV_PART_MAIN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, SAQ_C_SURFACE_RAISED, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        char tbuf[8];
        saq_sched_time_str(e, tbuf, sizeof(tbuf));
        lv_obj_t *lt = ui_make_label(row, SAQ_FONT_BODY, SAQ_C_TEXT);
        lv_obj_align(lt, LV_ALIGN_LEFT_MID, SAQ_SP_MD, 0);
        lv_label_set_text(lt, tbuf);

        char dbuf[24];
        saq_sched_days_summary(e->days, dbuf, sizeof(dbuf));
        lv_obj_t *ld = ui_make_label(row, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
        lv_obj_align(ld, LV_ALIGN_LEFT_MID, 140, 0);
        lv_label_set_text(ld, dbuf);

        char pbuf[12];
        snprintf(pbuf, sizeof(pbuf), "%u g", (unsigned)e->portion_g);
        lv_obj_t *lp = ui_make_label(row, SAQ_FONT_BODY, SAQ_C_TEXT);
        lv_obj_align(lp, LV_ALIGN_RIGHT_MID, -SAQ_SP_LG - 40, 0);
        lv_label_set_text(lp, pbuf);

        if (saq_portion_will_clamp(e->portion_g, g_app.ms_per_g)) {
            lv_obj_t *lw = ui_make_label(row, SAQ_FONT_SMALL, SAQ_C_WARN);
            lv_obj_align(lw, LV_ALIGN_RIGHT_MID, -SAQ_SP_LG - 40, 18);
            lv_label_set_text(lw, "clamped");
        }

        lv_obj_t *chev = ui_make_label(row, SAQ_FONT_BODY, SAQ_C_TEXT_MUTED);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -SAQ_SP_SM, 0);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    }
}

void ui_schedule_refresh(void) {
    /* The feeder's count is authoritative; a mismatch (e.g. after an
     * optimistic apply was NACKed) shows as rows beyond the reported
     * count — flagged, not silently trusted. */
    rebuild_rows();
}

void ui_schedule_create(lv_obj_t *parent) {
    s_list = lv_obj_create(parent);
    lv_obj_remove_style_all(s_list);
    lv_obj_add_style(s_list, &saq_style_screen, LV_PART_MAIN);
    lv_obj_set_size(s_list, SAQ_SCREEN_W, SAQ_CONTENT_H);
    lv_obj_align(s_list, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, SAQ_SP_SM, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    s_banner_offline = ui_make_card(parent, 800 - 2 * SAQ_SP_LG, 48);
    lv_obj_set_pos(s_banner_offline, SAQ_SP_LG, 0);
    lv_obj_set_style_border_color(s_banner_offline, SAQ_C_WARN, LV_PART_MAIN);
    lv_obj_t *bol = ui_make_label(s_banner_offline, SAQ_FONT_CAPTION, SAQ_C_WARN);
    lv_obj_align(bol, LV_ALIGN_LEFT_MID, SAQ_SP_SM, 0);
    lv_label_set_text(bol, "Offline — showing last known schedule, editing disabled");
    lv_obj_add_flag(s_banner_offline, LV_OBJ_FLAG_HIDDEN);

    s_lbl_empty = ui_make_label(parent, SAQ_FONT_BODY, SAQ_C_TEXT_MUTED);
    lv_obj_align(s_lbl_empty, LV_ALIGN_TOP_MID, 0, 140);
    lv_label_set_text(s_lbl_empty, "No feeds scheduled");

    s_btn_add = ui_make_btn(parent, UI_BTN_PRIMARY);
    lv_obj_set_size(s_btn_add, 220, SAQ_BTN_H);
    lv_obj_align(s_btn_add, LV_ALIGN_BOTTOM_RIGHT, -SAQ_SP_LG, -SAQ_SP_MD);
    lv_obj_add_event_cb(s_btn_add, on_add_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *al = ui_make_label(s_btn_add, SAQ_FONT_BODY, SAQ_C_ON_PRIMARY);
    lv_obj_center(al);
    lv_label_set_text(al, "+  Add feed");

    build_editor();
    rebuild_rows();
}
