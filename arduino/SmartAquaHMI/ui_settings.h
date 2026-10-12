/**
 * ui_settings.h — configuration, pairing, calibration, diagnostics door
 * (design/screens.md § Settings).
 */

#pragma once
#include "lvgl.h"
#include <stdint.h>

void ui_settings_create(lv_obj_t *parent);
void ui_settings_refresh(void);

/* Events from the link dispatcher (under the LVGL lock). */
void ui_settings_on_beacon(const uint8_t mac[6], uint8_t fw_major, uint8_t fw_minor);
void ui_settings_on_paired(void);
void ui_settings_on_cal_feed_done(uint16_t portion_g, uint16_t actual_ms, uint8_t result);
