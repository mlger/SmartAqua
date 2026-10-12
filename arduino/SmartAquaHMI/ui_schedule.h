/**
 * ui_schedule.h — the schedule list + editor (screens.md § Schedule,
 * § ScheduleEditor).
 */

#pragma once
#include "lvgl.h"

void ui_schedule_create(lv_obj_t *parent);
void ui_schedule_refresh(void);

/* Event from the link dispatcher when SET_SCHEDULE_ENTRY resolves. */
void ui_schedule_on_cmd_ack(bool ok, uint8_t nack_reason);
