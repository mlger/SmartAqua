/**
 * ui_theme.cpp — token transcription + shared styles. See ui_theme.h.
 *
 * LVGL 8 note: styles here are stateless (default state). Per-state looks
 * (pressed, disabled, checked) are applied as object-level setters with a
 * part|state selector inside ui_make_btn(), which is the idiomatic v8 way.
 */

#include "ui_theme.h"

lv_style_t saq_style_screen;
lv_style_t saq_style_card;
lv_style_t saq_style_card_press;
lv_style_t saq_style_btn_primary;
lv_style_t saq_style_btn_danger;
lv_style_t saq_style_btn_ghost;
lv_style_t saq_style_tab_btn;
lv_style_t saq_style_tab_btn_active;
lv_style_t saq_style_lbl_heading;
lv_style_t saq_style_lbl_body;
lv_style_t saq_style_lbl_caption;
lv_style_t saq_style_lbl_muted_val;
lv_style_t saq_style_input;

void ui_theme_init(void)
{
    /* Screen background */
    lv_style_init(&saq_style_screen);
    lv_style_set_bg_color(&saq_style_screen, SAQ_C_BG);
    lv_style_set_bg_opa(&saq_style_screen, LV_OPA_COVER);
    lv_style_set_pad_all(&saq_style_screen, 0);
    lv_style_set_border_width(&saq_style_screen, 0);

    /* Card: surface + visible 1px border (tokens.md: borders, not shadows) */
    lv_style_init(&saq_style_card);
    lv_style_set_bg_color(&saq_style_card, SAQ_C_SURFACE);
    lv_style_set_bg_opa(&saq_style_card, LV_OPA_COVER);
    lv_style_set_border_color(&saq_style_card, SAQ_C_BORDER);
    lv_style_set_border_width(&saq_style_card, 1);
    lv_style_set_radius(&saq_style_card, SAQ_R_MD);
    lv_style_set_pad_all(&saq_style_card, SAQ_SP_MD);

    /* Tappable-card pressed look — add with LV_STATE_PRESSED selector */
    lv_style_init(&saq_style_card_press);
    lv_style_set_bg_color(&saq_style_card_press, SAQ_C_SURFACE_RAISED);
    lv_style_set_border_color(&saq_style_card_press, SAQ_C_PRIMARY);

    /* Primary button — text on primary is dark (tokens.md contrast note) */
    lv_style_init(&saq_style_btn_primary);
    lv_style_set_bg_color(&saq_style_btn_primary, SAQ_C_PRIMARY);
    lv_style_set_bg_opa(&saq_style_btn_primary, LV_OPA_COVER);
    lv_style_set_text_color(&saq_style_btn_primary, SAQ_C_ON_PRIMARY);
    lv_style_set_border_width(&saq_style_btn_primary, 0);
    lv_style_set_radius(&saq_style_btn_primary, SAQ_R_MD);
    lv_style_set_shadow_width(&saq_style_btn_primary, 0);
    lv_style_set_pad_top(&saq_style_btn_primary, SAQ_SP_SM);
    lv_style_set_pad_bottom(&saq_style_btn_primary, SAQ_SP_SM);

    /* Danger button — destructive, always behind a confirmation */
    lv_style_init(&saq_style_btn_danger);
    lv_style_set_bg_color(&saq_style_btn_danger, SAQ_C_DANGER);
    lv_style_set_bg_opa(&saq_style_btn_danger, LV_OPA_COVER);
    lv_style_set_text_color(&saq_style_btn_danger, SAQ_C_TEXT);
    lv_style_set_border_width(&saq_style_btn_danger, 0);
    lv_style_set_radius(&saq_style_btn_danger, SAQ_R_MD);
    lv_style_set_shadow_width(&saq_style_btn_danger, 0);
    lv_style_set_pad_top(&saq_style_btn_danger, SAQ_SP_SM);
    lv_style_set_pad_bottom(&saq_style_btn_danger, SAQ_SP_SM);

    /* Ghost button (secondary actions) */
    lv_style_init(&saq_style_btn_ghost);
    lv_style_set_bg_color(&saq_style_btn_ghost, SAQ_C_SURFACE);
    lv_style_set_bg_opa(&saq_style_btn_ghost, LV_OPA_COVER);
    lv_style_set_text_color(&saq_style_btn_ghost, SAQ_C_TEXT);
    lv_style_set_border_color(&saq_style_btn_ghost, SAQ_C_BORDER);
    lv_style_set_border_width(&saq_style_btn_ghost, 1);
    lv_style_set_radius(&saq_style_btn_ghost, SAQ_R_MD);
    lv_style_set_shadow_width(&saq_style_btn_ghost, 0);
    lv_style_set_pad_top(&saq_style_btn_ghost, SAQ_SP_SM);
    lv_style_set_pad_bottom(&saq_style_btn_ghost, SAQ_SP_SM);

    /* Tab bar buttons */
    lv_style_init(&saq_style_tab_btn);
    lv_style_set_bg_color(&saq_style_tab_btn, SAQ_C_SURFACE);
    lv_style_set_bg_opa(&saq_style_tab_btn, LV_OPA_COVER);
    lv_style_set_text_color(&saq_style_tab_btn, SAQ_C_TEXT_MUTED);
    lv_style_set_border_width(&saq_style_tab_btn, 0);
    lv_style_set_radius(&saq_style_tab_btn, SAQ_R_MD);
    lv_style_set_shadow_width(&saq_style_tab_btn, 0);

    /* Active tab look — add with LV_STATE_CHECKED selector */
    lv_style_init(&saq_style_tab_btn_active);
    lv_style_set_text_color(&saq_style_tab_btn_active, SAQ_C_PRIMARY);
    lv_style_set_border_color(&saq_style_tab_btn_active, SAQ_C_PRIMARY);
    lv_style_set_border_side(&saq_style_tab_btn_active, LV_BORDER_SIDE_TOP);
    lv_style_set_border_width(&saq_style_tab_btn_active, 3);

    /* Label presets */
    lv_style_init(&saq_style_lbl_heading);
    lv_style_set_text_font(&saq_style_lbl_heading, SAQ_FONT_HEADING);
    lv_style_set_text_color(&saq_style_lbl_heading, SAQ_C_TEXT);

    lv_style_init(&saq_style_lbl_body);
    lv_style_set_text_font(&saq_style_lbl_body, SAQ_FONT_BODY);
    lv_style_set_text_color(&saq_style_lbl_body, SAQ_C_TEXT);

    lv_style_init(&saq_style_lbl_caption);
    lv_style_set_text_font(&saq_style_lbl_caption, SAQ_FONT_CAPTION);
    lv_style_set_text_color(&saq_style_lbl_caption, SAQ_C_TEXT_MUTED);

    /* Muted value — big number in muted colour (stale states) */
    lv_style_init(&saq_style_lbl_muted_val);
    lv_style_set_text_font(&saq_style_lbl_muted_val, SAQ_FONT_HERO);
    lv_style_set_text_color(&saq_style_lbl_muted_val, SAQ_C_TEXT_MUTED);

    /* Inputs (rollers, sliders inherit the theme through these) */
    lv_style_init(&saq_style_input);
    lv_style_set_bg_color(&saq_style_input, SAQ_C_BG);
    lv_style_set_bg_opa(&saq_style_input, LV_OPA_COVER);
    lv_style_set_border_color(&saq_style_input, SAQ_C_BORDER);
    lv_style_set_border_width(&saq_style_input, 1);
    lv_style_set_radius(&saq_style_input, SAQ_R_MD);
}

lv_obj_t *ui_make_btn(lv_obj_t *parent, ui_btn_kind_t kind)
{
    lv_obj_t *b = lv_btn_create(parent);
    switch (kind) {
    case UI_BTN_PRIMARY:
        lv_obj_add_style(b, &saq_style_btn_primary, LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, SAQ_C_PRIMARY_PRESSED, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, SAQ_C_BORDER, LV_PART_MAIN | LV_STATE_DISABLED);
        lv_obj_set_style_text_color(b, SAQ_C_TEXT_MUTED, LV_PART_MAIN | LV_STATE_DISABLED);
        break;
    case UI_BTN_DANGER:
        lv_obj_add_style(b, &saq_style_btn_danger, LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, lv_color_darken(SAQ_C_DANGER, LV_OPA_30),
                                  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, SAQ_C_BORDER, LV_PART_MAIN | LV_STATE_DISABLED);
        lv_obj_set_style_text_color(b, SAQ_C_TEXT_MUTED, LV_PART_MAIN | LV_STATE_DISABLED);
        break;
    case UI_BTN_TAB:
        lv_obj_add_style(b, &saq_style_tab_btn, LV_PART_MAIN);
        lv_obj_add_style(b, &saq_style_tab_btn_active, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(b, SAQ_C_SURFACE_RAISED, LV_PART_MAIN | LV_STATE_PRESSED);
        break;
    case UI_BTN_GHOST:
    default:
        lv_obj_add_style(b, &saq_style_btn_ghost, LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, SAQ_C_SURFACE_RAISED, LV_PART_MAIN | LV_STATE_PRESSED);
        break;
    }
    return b;
}

lv_obj_t *ui_make_card(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &saq_style_card, LV_PART_MAIN);
    lv_obj_set_size(card, w, h);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *ui_make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    return lbl;
}
