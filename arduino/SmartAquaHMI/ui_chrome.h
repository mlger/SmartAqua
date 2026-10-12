/**
 * ui_chrome.h — the shared frame every screen sits in (design/screens.md
 * § "Shared chrome"): 40 px status bar, 376 px content, 64 px tab bar.
 *
 * Also owns the cross-screen primitives: toasts, the confirmation modal
 * (on lv_layer_top, above everything), pushed overlays (ScheduleEditor,
 * History, Diagnostics) and the 1 s chrome timer that keeps the clock,
 * link state and clock-confidence indicators honest.
 */

#pragma once

#include "lvgl.h"
#include <stdbool.h>

/* Tab indices */
#define UI_TAB_HOME      0
#define UI_TAB_FEED      1
#define UI_TAB_SCHED     2
#define UI_TAB_SETTINGS  3

void ui_init(void);
void ui_nav_tab(int idx);

/* Container of tab idx (screens create their widgets inside). */
lv_obj_t *ui_tab_container(int idx);

/* While a feed is running the FeedNow screen must not be dismissable —
 * the tab bar locks (screens.md § FeedNow, "Running"). */
void ui_tabbar_enable(bool enable);

/* Toast kinds */
#define UI_TOAST_OK      0
#define UI_TOAST_WARN    1
#define UI_TOAST_DANGER  2

void ui_toast(const char *text, int kind);

/* Confirmation modal — every feed and every destructive action goes
 * through one of these (screens.md: "no single-tap feed anywhere"). */
typedef void (*ui_confirm_cb_t)(bool yes, void *user);
void ui_confirm(const char *title, const char *body,
                const char *yes_label, const char *no_label,
                bool yes_is_danger, ui_confirm_cb_t cb, void *user);

/* Overlay factory: full-screen push with a back arrow, covering the chrome.
 * Returns the content container (starts empty, 800x440 below its header). */
lv_obj_t *ui_overlay_create(const char *title);
void ui_overlay_set_title(lv_obj_t *overlay_content, const char *title);
void ui_show_overlay(lv_obj_t *ov);
void ui_hide_overlay(void);
lv_obj_t *ui_current_overlay(void);

/* Periodic refresh entry points (called by the app dispatcher, under the
 * LVGL lock). The 1 s chrome timer is internal. */
void ui_chrome_refresh(void);
