/**
 * ui_diagnostics.h — the screen you are grateful for when something is
 * subtly wrong (design/screens.md § Diagnostics).
 */

#pragma once
#include "lvgl.h"

void ui_diagnostics_create(void);
void ui_diagnostics_refresh(void);
lv_obj_t *ui_diagnostics_get(void);
