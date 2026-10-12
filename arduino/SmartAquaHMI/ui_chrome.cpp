/**
 * ui_chrome.cpp — see ui_chrome.h.
 */

#include "ui_chrome.h"
#include "ui_theme.h"
#include "app_model.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Chrome widgets                                                      */
/* ------------------------------------------------------------------ */

static lv_obj_t *s_status_bar;
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_dot_link;
static lv_obj_t *s_lbl_link;
static lv_obj_t *s_lbl_clock_conf;
static lv_obj_t *s_lbl_time;

static lv_obj_t *s_content;
static lv_obj_t *s_tab_bar;
static lv_obj_t *s_tab_btns[4];
static lv_obj_t *s_tab_conts[4];
static int        s_active_tab = UI_TAB_HOME;

static lv_obj_t *s_current_overlay = NULL;
static lv_obj_t *s_overlay_clocks[4];
static int       s_overlay_clock_n = 0;

static const char *TAB_NAMES[4] = {"Home", "Feed", "Schedule", "Settings"};

/* ------------------------------------------------------------------ */
/* Screens (implemented in their own files)                            */
/* ------------------------------------------------------------------ */

void ui_home_create(lv_obj_t *parent);
void ui_home_refresh(void);
void ui_feednow_create(lv_obj_t *parent);
void ui_schedule_create(lv_obj_t *parent);
void ui_schedule_refresh(void);
void ui_settings_create(lv_obj_t *parent);
void ui_settings_refresh(void);
void ui_history_create(void);      /* overlay */
void ui_diagnostics_create(void);  /* overlay */

/* ------------------------------------------------------------------ */
/* Tab navigation                                                      */
/* ------------------------------------------------------------------ */

static void on_tab_btn(lv_event_t *e)
{
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_nav_tab(idx);
}

void ui_nav_tab(int idx)
{
    if ((idx < 0) || (idx > 3)) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        lv_obj_add_flag(s_tab_conts[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(s_tab_btns[i], LV_STATE_CHECKED);
    }
    lv_obj_clear_flag(s_tab_conts[idx], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(s_tab_btns[idx], LV_STATE_CHECKED);
    s_active_tab = idx;
    lv_label_set_text(s_lbl_title, TAB_NAMES[idx]);
}

lv_obj_t *ui_tab_container(int idx)
{
    return s_tab_conts[idx];
}

void ui_tabbar_enable(bool enable)
{
    for (int i = 0; i < 4; i++) {
        if (enable) {
            lv_obj_clear_state(s_tab_btns[i], LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_tab_btns[i], LV_STATE_DISABLED);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Status bar refresh (1 s)                                            */
/* ------------------------------------------------------------------ */

static void refresh_link_indicator(void)
{
    lv_color_t dot = SAQ_C_TEXT_MUTED;
    const char *text = "Not paired";
    if (g_app.paired && g_app.link_up) {
        dot = SAQ_C_OK;
        text = "Feeder";
    } else if (g_app.paired) {
        dot = SAQ_C_DANGER;
        text = "Offline";
    } else {
        dot = SAQ_C_WARN;
        text = "Not paired";
    }
    lv_obj_set_style_bg_color(s_dot_link, dot, LV_PART_MAIN);
    lv_label_set_text(s_lbl_link, text);
}

static void refresh_clock(void)
{
    char buf[16];
    if (g_app.clock_ok) {
        const uint32_t loc = model_local_epoch();
        const uint32_t sod = loc % 86400u;
        snprintf(buf, sizeof(buf), "%02u:%02u",
                 (unsigned)(sod / 3600u), (unsigned)((sod % 3600u) / 60u));
        lv_obj_set_style_text_color(s_lbl_time, SAQ_C_TEXT_MUTED, LV_PART_MAIN);
        lv_obj_add_flag(s_lbl_clock_conf, LV_OBJ_FLAG_HIDDEN);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
        lv_obj_set_style_text_color(s_lbl_time, SAQ_C_WARN, LV_PART_MAIN);
        lv_obj_clear_flag(s_lbl_clock_conf, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_lbl_time, buf);

    for (int i = 0; i < s_overlay_clock_n; i++) {
        if (buf[0] == '-') {
            lv_label_set_text(s_overlay_clocks[i], "--:--");
        } else {
            lv_label_set_text(s_overlay_clocks[i], buf);
        }
    }
}

static void on_chrome_timer(lv_timer_t *t)
{
    (void)t;
    refresh_link_indicator();
    refresh_clock();
}

void ui_chrome_refresh(void)
{
    refresh_link_indicator();
    refresh_clock();
}

/* ------------------------------------------------------------------ */
/* Toast                                                               */
/* ------------------------------------------------------------------ */

static void on_toast_expire(lv_timer_t *t)
{
    lv_obj_t *toast = (lv_obj_t *)t->user_data;
    lv_obj_del(toast);
    lv_timer_del(t);
}

void ui_toast(const char *text, int kind)
{
    lv_obj_t *toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(toast);
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(toast, SAQ_R_PILL, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(toast, SAQ_SP_LG, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(toast, SAQ_SP_SM, LV_PART_MAIN);
    lv_obj_set_style_border_width(toast, 0, LV_PART_MAIN);

    switch (kind) {
    case UI_TOAST_OK:      lv_obj_set_style_bg_color(toast, SAQ_C_OK, LV_PART_MAIN); break;
    case UI_TOAST_WARN:    lv_obj_set_style_bg_color(toast, SAQ_C_WARN, LV_PART_MAIN); break;
    case UI_TOAST_DANGER:  lv_obj_set_style_bg_color(toast, SAQ_C_DANGER, LV_PART_MAIN); break;
    default:               lv_obj_set_style_bg_color(toast, SAQ_C_SURFACE_RAISED, LV_PART_MAIN); break;
    }

    lv_obj_t *lbl = lv_label_create(toast);
    lv_obj_set_style_text_font(lbl, SAQ_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, (kind == UI_TOAST_WARN) ? SAQ_C_TEXT : SAQ_C_BG, LV_PART_MAIN);
    lv_label_set_text(lbl, text);

    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -(SAQ_TABBAR_H + SAQ_SP_LG));
    (void)lv_timer_create(on_toast_expire, 3000, toast);   /* auto-dismiss 3 s */
}

/* ------------------------------------------------------------------ */
/* Confirmation modal                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    ui_confirm_cb_t cb;
    void *user;
    lv_obj_t *root;
    lv_obj_t *btn_yes;
} confirm_ctx_t;

static void on_confirm_btn(lv_event_t *e)
{
    confirm_ctx_t *ctx = (confirm_ctx_t *)lv_event_get_user_data(e);
    const bool is_yes = (lv_event_get_target(e) == ctx->btn_yes);
    ui_confirm_cb_t cb = ctx->cb;
    void *user = ctx->user;
    lv_obj_del(ctx->root);       /* removes scrim, panel, buttons */
    lv_mem_free(ctx);
    if (cb != NULL) {
        cb(is_yes, user);
    }
}

void ui_confirm(const char *title, const char *body,
                const char *yes_label, const char *no_label,
                bool yes_is_danger, ui_confirm_cb_t cb, void *user)
{
    confirm_ctx_t *ctx = (confirm_ctx_t *)lv_mem_alloc(sizeof(*ctx));
    if (ctx == NULL) {
        return;
    }
    ctx->cb = cb;
    ctx->user = user;

    /* Scrim: black at 60%, swallows input behind the modal */
    lv_obj_t *scrim = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, SAQ_SCREEN_W, SAQ_SCREEN_H);
    lv_obj_set_style_bg_color(scrim, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(scrim, 0, LV_PART_MAIN);
    ctx->root = scrim;

    /* Panel: 400 wide max, raised surface, fixed height sized for a
     * two-line body (tokens.md component sizes). */
    lv_obj_t *panel = lv_obj_create(scrim);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 400, 240);
    lv_obj_set_style_bg_color(panel, SAQ_C_SURFACE_RAISED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, SAQ_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, SAQ_R_MD, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SAQ_SP_LG, LV_PART_MAIN);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = ui_make_label(panel, SAQ_FONT_HEADING, SAQ_C_TEXT);
    lv_label_set_text(lbl_title, title);

    lv_obj_t *lbl_body = ui_make_label(panel, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_label_set_text(lbl_body, body);
    lv_label_set_long_mode(lbl_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_body, 400 - 2 * SAQ_SP_LG);
    lv_obj_align(lbl_body, LV_ALIGN_TOP_LEFT, 0, SAQ_SP_XL);

    /* Buttons: No on the left, Yes on the right */
    lv_obj_t *btn_no = ui_make_btn(panel, UI_BTN_GHOST);
    lv_obj_set_size(btn_no, 168, SAQ_BTN_H);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *l1 = ui_make_label(btn_no, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_label_set_text(l1, no_label);
    lv_obj_center(l1);

    lv_obj_t *btn_yes = ui_make_btn(panel, yes_is_danger ? UI_BTN_DANGER : UI_BTN_PRIMARY);
    lv_obj_set_size(btn_yes, 168, SAQ_BTN_H);
    lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *l2 = ui_make_label(btn_yes, SAQ_FONT_BODY,
                                  yes_is_danger ? SAQ_C_TEXT : SAQ_C_ON_PRIMARY);
    lv_label_set_text(l2, yes_label);
    lv_obj_center(l2);

    /* The panel is 240 px: title zone + body + 56 px buttons. */
    lv_obj_add_event_cb(btn_no, on_confirm_btn, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(btn_yes, on_confirm_btn, LV_EVENT_CLICKED, ctx);
    ctx->btn_yes = btn_yes;
}

/* ------------------------------------------------------------------ */
/* Overlays                                                            */
/* ------------------------------------------------------------------ */

static void on_overlay_back(lv_event_t *e)
{
    (void)e;
    ui_hide_overlay();
}

lv_obj_t *ui_overlay_create(const char *title)
{
    lv_obj_t *ov = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(ov);
    lv_obj_add_style(ov, &saq_style_screen, LV_PART_MAIN);
    lv_obj_set_size(ov, SAQ_SCREEN_W, SAQ_SCREEN_H);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    /* Header */
    lv_obj_t *btn_back = ui_make_btn(ov, UI_BTN_GHOST);
    lv_obj_set_size(btn_back, 48, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, SAQ_SP_SM, 0);
    lv_obj_add_event_cb(btn_back, on_overlay_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = ui_make_label(btn_back, SAQ_FONT_BODY, SAQ_C_TEXT);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);

    lv_obj_t *lbl_title = ui_make_label(ov, SAQ_FONT_HEADING, SAQ_C_TEXT);
    lv_label_set_text(lbl_title, title);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 72, SAQ_SP_XS);

    if (s_overlay_clock_n < 4) {
        lv_obj_t *clock = ui_make_label(ov, SAQ_FONT_BODY, SAQ_C_TEXT_MUTED);
        lv_obj_align(clock, LV_ALIGN_TOP_RIGHT, -SAQ_SP_LG, SAQ_SP_XS);
        lv_label_set_text(clock, "--:--");
        s_overlay_clocks[s_overlay_clock_n++] = clock;
    }

    /* Content area below the header */
    lv_obj_t *content = lv_obj_create(ov);
    lv_obj_remove_style_all(content);
    lv_obj_add_style(content, &saq_style_screen, LV_PART_MAIN);
    lv_obj_set_size(content, SAQ_SCREEN_W, SAQ_SCREEN_H - SAQ_STATUSBAR_H);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    return content;
}

void ui_overlay_set_title(lv_obj_t *overlay_content, const char *title)
{
    if (overlay_content == NULL) {
        return;
    }
    /* overlay children: [0] back button, [1] title label, [2] clock, [3] content */
    lv_obj_t *ov = lv_obj_get_parent(overlay_content);
    if (ov == NULL) {
        return;
    }
    lv_obj_t *lbl = lv_obj_get_child(ov, 1);
    if (lbl != NULL) {
        lv_label_set_text(lbl, title);
    }
}

void ui_show_overlay(lv_obj_t *ov)
{
    if (s_current_overlay != NULL) {
        lv_obj_add_flag(s_current_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    s_current_overlay = ov;
    if (ov != NULL) {
        lv_obj_clear_flag(ov, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ov);
    }
}

void ui_hide_overlay(void)
{
    if (s_current_overlay != NULL) {
        lv_obj_add_flag(s_current_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    s_current_overlay = NULL;
}

lv_obj_t *ui_current_overlay(void)
{
    return s_current_overlay;
}

/* ------------------------------------------------------------------ */
/* Build                                                               */
/* ------------------------------------------------------------------ */

static void on_clock_conf_click(lv_event_t *e)
{
    (void)e;
    ui_nav_tab(UI_TAB_SETTINGS);
}

void ui_init(void)
{
    ui_theme_init();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_add_style(scr, &saq_style_screen, LV_PART_MAIN);

    /* ---- status bar ---- */
    s_status_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_status_bar);
    lv_obj_set_size(s_status_bar, SAQ_SCREEN_W, SAQ_STATUSBAR_H);
    lv_obj_align(s_status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_status_bar, SAQ_C_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_status_bar, SAQ_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_status_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_status_bar, 1, LV_PART_MAIN);
    lv_obj_clear_flag(s_status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_title = ui_make_label(s_status_bar, SAQ_FONT_HEADING, SAQ_C_TEXT);
    lv_obj_align(s_lbl_title, LV_ALIGN_LEFT_MID, SAQ_SP_LG, 0);
    lv_label_set_text(s_lbl_title, "Home");

    s_lbl_clock_conf = ui_make_label(s_status_bar, SAQ_FONT_CAPTION, SAQ_C_WARN);
    lv_obj_align(s_lbl_clock_conf, LV_ALIGN_RIGHT_MID, -300, 0);
    lv_label_set_text(s_lbl_clock_conf, "Time?");
    lv_obj_add_flag(s_lbl_clock_conf, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lbl_clock_conf, on_clock_conf_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_lbl_clock_conf, LV_OBJ_FLAG_HIDDEN);

    s_lbl_time = ui_make_label(s_status_bar, SAQ_FONT_BODY, SAQ_C_TEXT_MUTED);
    lv_obj_align(s_lbl_time, LV_ALIGN_RIGHT_MID, -SAQ_SP_LG, 0);
    lv_label_set_text(s_lbl_time, "--:--");

    s_lbl_link = ui_make_label(s_status_bar, SAQ_FONT_CAPTION, SAQ_C_TEXT_MUTED);
    lv_obj_align(s_lbl_link, LV_ALIGN_RIGHT_MID, -SAQ_SP_LG - 140, 0);
    lv_label_set_text(s_lbl_link, "Not paired");

    s_dot_link = lv_obj_create(s_status_bar);
    lv_obj_remove_style_all(s_dot_link);
    lv_obj_set_size(s_dot_link, 10, 10);
    lv_obj_set_style_radius(s_dot_link, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dot_link, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dot_link, SAQ_C_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_align(s_dot_link, LV_ALIGN_RIGHT_MID, -SAQ_SP_LG - 230, 0);

    /* ---- content area with four tab containers ---- */
    s_content = lv_obj_create(scr);
    lv_obj_remove_style_all(s_content);
    lv_obj_add_style(s_content, &saq_style_screen, LV_PART_MAIN);
    lv_obj_set_size(s_content, SAQ_SCREEN_W, SAQ_CONTENT_H);
    lv_obj_align(s_content, LV_ALIGN_TOP_MID, 0, SAQ_STATUSBAR_H);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) {
        s_tab_conts[i] = lv_obj_create(s_content);
        lv_obj_remove_style_all(s_tab_conts[i]);
        lv_obj_add_style(s_tab_conts[i], &saq_style_screen, LV_PART_MAIN);
        lv_obj_set_size(s_tab_conts[i], SAQ_SCREEN_W, SAQ_CONTENT_H);
        lv_obj_align(s_tab_conts[i], LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_clear_flag(s_tab_conts[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_tab_conts[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* ---- tab bar ---- */
    s_tab_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_tab_bar);
    lv_obj_set_size(s_tab_bar, SAQ_SCREEN_W, SAQ_TABBAR_H);
    lv_obj_align(s_tab_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_tab_bar, SAQ_C_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_tab_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_tab_bar, SAQ_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_tab_bar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tab_bar, 1, LV_PART_MAIN);
    lv_obj_clear_flag(s_tab_bar, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) {
        s_tab_btns[i] = ui_make_btn(s_tab_bar, UI_BTN_TAB);
        lv_obj_set_size(s_tab_btns[i], 188, SAQ_TABBAR_H - 2 * SAQ_SP_SM);
        lv_obj_align(s_tab_btns[i], LV_ALIGN_LEFT_MID, i * 192 + SAQ_SP_SM, 0);
        lv_obj_add_event_cb(s_tab_btns[i], on_tab_btn, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_flag(s_tab_btns[i], LV_OBJ_FLAG_CHECKABLE);

        lv_obj_t *lbl = ui_make_label(s_tab_btns[i], SAQ_FONT_BODY, SAQ_C_TEXT_MUTED);
        lv_label_set_text(lbl, TAB_NAMES[i]);
        lv_obj_center(lbl);
    }

    /* ---- screens ---- */
    ui_home_create(s_tab_conts[UI_TAB_HOME]);
    ui_feednow_create(s_tab_conts[UI_TAB_FEED]);
    ui_schedule_create(s_tab_conts[UI_TAB_SCHED]);
    ui_settings_create(s_tab_conts[UI_TAB_SETTINGS]);
    ui_history_create();
    ui_diagnostics_create();

    /* overlays live above the chrome; created hidden */
    if (s_current_overlay != NULL) {
        s_current_overlay = NULL;
    }

    (void)lv_timer_create(on_chrome_timer, 1000, NULL);
    ui_nav_tab(UI_TAB_HOME);
    ui_chrome_refresh();
}
