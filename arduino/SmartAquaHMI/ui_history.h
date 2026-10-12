/**
 * ui_history.h — what actually happened, including while the HMI was
 * offline (design/screens.md § History). Reconciled from the feeder's log.
 */

#pragma once
#include "lvgl.h"

void ui_history_create(void);
void ui_history_refresh(void);
lv_obj_t *ui_history_get(void);
