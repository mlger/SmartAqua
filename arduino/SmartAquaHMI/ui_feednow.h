/**
 * ui_feednow.h — manual feed with deliberate confirmation (screens.md § FeedNow).
 */

#pragma once
#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

void ui_feednow_create(lv_obj_t *parent);

/* Events from the link dispatcher (called under the LVGL lock). */
void ui_feednow_on_cmd_ack(bool ok, uint8_t nack_reason);
void ui_feednow_on_feed_done(uint16_t portion_g, uint16_t actual_ms,
                             uint8_t trigger, uint8_t result);
void ui_feednow_on_link(bool up);
