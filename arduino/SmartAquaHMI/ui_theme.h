/**
 * ui_theme.h — design tokens as C, transcribed 1:1 from design/tokens.md.
 *
 * The rules (docs/06 § Tokens): a grep for a hex literal outside this file
 * must return nothing; Figma variable `colour/primary` is `SAQ_C_PRIMARY`;
 * lowercase-hyphens becomes SAQ_<GROUP>_<NAME>. If this file and Figma
 * disagree, this file ships.
 */

#pragma once

#include "lvgl.h"

/* ---- Colour (RGB565 panel; LVGL quantises on assignment) ------------- */

#define SAQ_C_BG              lv_color_hex(0x0E1621)
#define SAQ_C_SURFACE         lv_color_hex(0x1B2735)
#define SAQ_C_SURFACE_RAISED  lv_color_hex(0x243447)
#define SAQ_C_BORDER          lv_color_hex(0x2E4057)
#define SAQ_C_PRIMARY         lv_color_hex(0x2E9BD6)
#define SAQ_C_PRIMARY_PRESSED lv_color_hex(0x1F7BAA)
#define SAQ_C_ON_PRIMARY      lv_color_hex(0x0E1621)
#define SAQ_C_TEXT            lv_color_hex(0xF2F6FA)
#define SAQ_C_TEXT_MUTED      lv_color_hex(0x93A4B8)
#define SAQ_C_OK              lv_color_hex(0x3FB950)
#define SAQ_C_WARN            lv_color_hex(0xD29922)
#define SAQ_C_DANGER          lv_color_hex(0xF85149)
/* SCRIM: black at 60% — use lv_color_mix(SAQ_C_SCRIM_BASE, ...) helpers */

/* ---- Spacing — 4 px base scale --------------------------------------- */

#define SAQ_SP_XS   4
#define SAQ_SP_SM   8
#define SAQ_SP_MD   16
#define SAQ_SP_LG   24
#define SAQ_SP_XL   32
#define SAQ_SP_2XL  48

/* ---- Radii ------------------------------------------------------------ */

#define SAQ_R_SM    4
#define SAQ_R_MD    8
#define SAQ_R_PILL  20

/* ---- Typography -------------------------------------------------------- */

/* Montserrat sizes enabled in lv_conf.h (tokens carry 16/20/24; 12 is the
 * diagnostics/hex font, 30 the Home readout). */
#define SAQ_FONT_SMALL    (&lv_font_montserrat_12)
#define SAQ_FONT_CAPTION  (&lv_font_montserrat_16)
#define SAQ_FONT_BODY     (&lv_font_montserrat_20)
#define SAQ_FONT_HEADING  (&lv_font_montserrat_24)
#define SAQ_FONT_HERO     (&lv_font_montserrat_30)

/* ---- Layout ------------------------------------------------------------ */

#define SAQ_SCREEN_W       800
#define SAQ_SCREEN_H       480
#define SAQ_STATUSBAR_H    40
#define SAQ_TABBAR_H       64
#define SAQ_CONTENT_H      (SAQ_SCREEN_H - SAQ_STATUSBAR_H - SAQ_TABBAR_H)  /* 376 */
#define SAQ_TOUCH_MIN      44
#define SAQ_BTN_H          56
#define SAQ_ROW_H          56
#define SAQ_EDGE_DEAD      20

/* ---- Shared styles (defined once in ui_theme.cpp) --------------------- */

extern lv_style_t saq_style_screen;
extern lv_style_t saq_style_card;
extern lv_style_t saq_style_card_press;
extern lv_style_t saq_style_btn_primary;
extern lv_style_t saq_style_btn_danger;
extern lv_style_t saq_style_btn_ghost;
extern lv_style_t saq_style_tab_btn;
extern lv_style_t saq_style_tab_btn_active;
extern lv_style_t saq_style_lbl_heading;
extern lv_style_t saq_style_lbl_body;
extern lv_style_t saq_style_lbl_caption;
extern lv_style_t saq_style_lbl_muted_val;
extern lv_style_t saq_style_input;

void ui_theme_init(void);

/* Convenience: card factory used by every screen. */
lv_obj_t *ui_make_card(lv_obj_t *parent, int32_t w, int32_t h);

/* Convenience: label with a font + colour preset. */
lv_obj_t *ui_make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color);

/* Button factory — applies the shared style plus the LVGL 8 per-state
 * looks (pressed / disabled / checked) that object-level selectors carry. */
typedef enum {
    UI_BTN_PRIMARY = 0,
    UI_BTN_DANGER,
    UI_BTN_GHOST,
    UI_BTN_TAB,
} ui_btn_kind_t;

lv_obj_t *ui_make_btn(lv_obj_t *parent, ui_btn_kind_t kind);
