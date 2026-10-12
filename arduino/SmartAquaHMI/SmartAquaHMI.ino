/**
 * SmartAquaHMI.ino — the touchscreen controller firmware
 * (Waveshare ESP32-S3-Touch-LCD-4.3B).
 *
 * Owns the LVGL UI, the PCF85063A clock, and the ESP-NOW command side of
 * the SmartAqua protocol. Owns nothing on the critical path from schedule
 * to motor — that is the feeder node's job (design rule 1).
 *
 * Display stack (vendored, proven):
 *   ESP32_Display_Panel + ESP32_IO_Expander + esp-lib-utils + LVGL 8.4,
 *   exactly as shipped in the Waveshare demo package. The board config in
 *   esp_panel_board_custom_conf.h carries two corrections for this board:
 *   800x480 (not the 1024x600 default the demo ships with) and touch ON.
 *
 * Threading: LVGL runs on its own task via lvgl_v8_port (mutex-protected).
 * Everything this sketch does to widgets happens under lvgl_port_lock();
 * nothing blocks while holding it (docs/06 § "Working with the LVGL task").
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>

#include <lvgl.h>
#include "lvgl_v8_port.h"

#include "app_model.h"
#include "saq_rtc.h"
#include "saq_link.h"
#include "saq_protocol.h"
#include "saq_schedule.h"
#include "SmartAquaHMI_version.h"

#include "ui_chrome.h"
#include "ui_home.h"
#include "ui_feednow.h"
#include "ui_schedule.h"
#include "ui_settings.h"
#include "ui_history.h"
#include "ui_diagnostics.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

/* ------------------------------------------------------------------ */
/* Link event dispatch                                                 */
/* ------------------------------------------------------------------ */

/* The candidate MAC remembered from the beacon the user is pairing with. */
static uint8_t s_pair_candidate[6];
static bool    s_have_candidate = false;

static void dispatch_link_event(const hmi_link_evt_t *e)
{
    switch (e->type) {
    case HMI_LINK_EVT_ACK_OK:
    case HMI_LINK_EVT_NACK:
    case HMI_LINK_EVT_TIMEOUT: {
        const bool ok = (e->type == HMI_LINK_EVT_ACK_OK);
        switch (e->cmd_type) {
        case SAQ_T_FEED:
            /* the calibration test feed is also a FEED command */
            ui_feednow_on_cmd_ack(ok, e->nack_reason);
            if (!ok) {
                ui_settings_on_cal_feed_done(0, 0, 0xFF);   /* resets the flow */
            }
            break;
        case SAQ_T_PAIR_REQ:
            if (ok && s_have_candidate) {
                link_finish_pairing(s_pair_candidate);
                ui_settings_on_paired();
                s_have_candidate = false;
            } else if (!ok) {
                ui_toast("Pairing refused", UI_TOAST_DANGER);
                link_set_pair_armed(false);
                ui_settings_refresh();
            }
            break;
        case SAQ_T_SET_SCHEDULE_ENTRY:
            ui_schedule_on_cmd_ack(ok, e->nack_reason);
            break;
        case SAQ_T_STOP:
            if (!ok) {
                ui_toast("STOP not confirmed — check the feeder", UI_TOAST_DANGER);
            }
            break;
        default:
            break;   /* SET_TIME / SET_CONFIG / STATUS_REQ acks are quiet */
        }
        break;
    }

    case HMI_LINK_EVT_STATUS:
        /* Adopt the feeder's config as the cache of record. */
        if (g_app.st_has_config) {
            g_app.ms_per_g = g_app.st_ms_per_g;
            g_app.default_portion_g = g_app.st_default_portion_g;
        }
        /* History reconciliation: the log grew beyond what we know. */
        if (g_app.st_last_log_seq > g_app.hist_last_seq) {
            (void)link_send_status_req(g_app.hist_last_seq);
        }
        ui_home_refresh();
        ui_schedule_refresh();
        ui_settings_refresh();
        ui_diagnostics_refresh();
        break;

    case HMI_LINK_EVT_FEED_DONE:
        ui_feednow_on_feed_done(e->feed_done.portion_g, e->feed_done.actual_ms,
                                e->feed_done.trigger, e->feed_done.result);
        ui_settings_on_cal_feed_done(e->feed_done.portion_g, e->feed_done.actual_ms,
                                     e->feed_done.result);
        /* the log entry proper arrives via LOG_ENTRY replay */
        (void)link_send_status_req(g_app.hist_last_seq);
        break;

    case HMI_LINK_EVT_LOG_ENTRY: {
        saq_hist_entry_t h;
        h.log_seq = e->log_entry.log_seq;
        h.event_epoch = e->log_entry.event_epoch;
        h.portion_g = e->log_entry.portion_g;
        h.actual_ms = e->log_entry.actual_ms;
        h.trigger = e->log_entry.trigger;
        h.result = e->log_entry.result;
        if (model_hist_add(&h)) {
            ui_home_refresh();
            if (ui_current_overlay() != NULL) {
                ui_history_refresh();
            }
        }
        break;
    }

    case HMI_LINK_EVT_FAULT:
        ui_toast("Feeder fault — see Diagnostics", UI_TOAST_DANGER);
        break;

    case HMI_LINK_EVT_BEACON:
        ui_settings_on_beacon(e->from_mac, e->beacon.fw_major, e->beacon.fw_minor);
        memcpy(s_pair_candidate, e->from_mac, 6);
        s_have_candidate = true;
        break;

    case HMI_LINK_EVT_LINK_UP:
        ui_toast("Feeder online", UI_TOAST_OK);
        ui_feednow_on_link(true);
        ui_home_refresh();
        ui_schedule_refresh();
        break;

    case HMI_LINK_EVT_LINK_DOWN:
        ui_feednow_on_link(false);
        ui_home_refresh();
        ui_schedule_refresh();
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Setup / loop                                                        */
/* ------------------------------------------------------------------ */

static Board *s_board = NULL;
static uint32_t s_last_rtc_poll_ms = 0;

void setup()
{
    Serial.begin(115200);
    delay(50);
    Serial.println();
    Serial.println("=== SmartAqua HMI (ESP32-S3-Touch-LCD-4.3B) ===");
    Serial.printf("fw %d.%d, protocol v%u.%u, LVGL %u.%u\n",
                  HMI_FW_MAJOR, HMI_FW_MINOR,
                  SAQ_PROTO_VERSION_MAJOR, SAQ_PROTO_VERSION_MINOR,
                  LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);

    /* ---- board + display (vendored stack) ---- */
    Serial.println("init board");
    s_board = new Board();
    s_board->init();

#if LVGL_PORT_AVOID_TEARING_MODE
    {
        auto lcd = s_board->getLCD();
        lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
        auto lcd_bus = lcd->getBus();
        if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
            static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
        }
#endif
    }
#endif

    assert(s_board->begin());
    Serial.println("init LVGL");
    lvgl_port_init(s_board->getLCD(), s_board->getTouch());

    /* ---- protocol self-test on the console, before anything moves ---- */
    {
        char report[2048];
        const int failures = saq_protocol_self_test(report, sizeof(report));
        Serial.printf("protocol self-test: %d failure(s)\n", failures);
        if (failures > 0) {
            Serial.println(report);
        }
    }

    /* ---- app state, clock, link ---- */
    model_init();
    rtc_init();
    g_app.clock_ok = rtc_valid();
    link_init();

    /* ---- UI (built once, under the lock like the vendor example) ---- */
    Serial.println("build UI");
    lvgl_port_lock(-1);
    ui_init();
    lvgl_port_unlock();

    /* ---- rejoin the feeder if we were paired ---- */
    if (g_app.paired) {
        (void)link_send_status_req(g_app.hist_last_seq);
        if (g_app.clock_ok) {
            (void)link_send_set_time(model_epoch(), model_tz_s());
        }
    }

    Serial.printf("free heap: %u B, free PSRAM: %u B\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    Serial.println("ready.");
}

void loop()
{
    /* RTC re-read hourly: keeps millis() drift out of the displayed clock
     * and out of SET_TIME pushes. */
    if ((millis() - s_last_rtc_poll_ms) > 3600000u) {
        s_last_rtc_poll_ms = millis();
        rtc_refresh();
        g_app.clock_ok = rtc_valid();
    }

    /* Drive the protocol engine, then drain its events into the UI. */
    link_tick();

    hmi_link_evt_t e;
    while (link_poll(&e)) {
        if (lvgl_port_lock(-1)) {
            dispatch_link_event(&e);
            lvgl_port_unlock();
        }
    }

    delay(5);
}
