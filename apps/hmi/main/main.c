/*
 * SmartAqua HMI — ESP32-S3-Touch-LCD-4.3B
 *
 * Scaffolding only. This boots and logs. There is no display, touch, RTC, or
 * ESP-NOW code here yet.
 *
 * Before implementing anything, work through bring-up steps 0-2 in
 * docs/08-testing-and-bringup.md. In particular, settle two facts about your
 * specific board revision first:
 *
 *   1. Panel resolution (800x480 or 1024x600)
 *   2. Panel controller (ST7262 or ST7701)
 *
 * Both are revision-dependent and both are ambiguous in the vendor package.
 * Flash the vendor prebuilt binaries to find out. Guessing wrong means every
 * LVGL buffer and every Figma frame is wrong.
 */

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>

static const char *TAG = "hmi/main";

static void log_board_facts(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "SmartAqua HMI (ESP32-S3-Touch-LCD-4.3B)");
    ESP_LOGI(TAG, "cores=%d  silicon rev=v%d.%d", chip.cores, chip.revision / 100, chip.revision % 100);
    ESP_LOGI(TAG, "flash=%" PRIu32 " MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "free heap=%" PRIu32 " B, free PSRAM=%" PRIu32 " B",
             esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Scaffolding only — no UI yet. See docs/09-roadmap-and-milestones.md (M1).");
}

void app_main(void)
{
    log_board_facts();

    /* Nothing to do yet. Deliberately no heartbeat log: the boot output is
     * enough to confirm the board is alive, and a periodic line here would
     * only add noise once real logging exists. */
    vTaskDelay(portMAX_DELAY);
}
