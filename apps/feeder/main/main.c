/*
 * SmartAqua feeder node — classic ESP32
 *
 * Scaffolding only. This boots and logs. There is no relay driver, no schedule,
 * no NVS code, and no ESP-NOW here yet.
 *
 * When the relay driver is written, two things in docs/01-architecture.md are
 * not negotiable:
 *
 *   Rule 2 — fail safe by construction. De-energised means motor off, and a
 *            hard MAX_FEED_MS clamp is enforced in the actuation layer, below
 *            the command layer, so no malformed command, hung task, or reboot
 *            can run the motor indefinitely.
 *
 *   Rule 4 — motor power is separate from logic power. The PSU wiring is in
 *            docs/02-hardware-and-wiring.md.
 *
 * Before wiring anything, run bring-up step 4 in docs/08-testing-and-bringup.md.
 * Verify these three, in order, with an LED and a meter before the motor is
 * connected:
 *
 *   1. The relay is SILENT through reset and boot.
 *   2. The relay drops out when the ESP32 loses power while energised.
 *   3. The relay drops out on a deliberate firmware crash while energised.
 *
 * If the relay clicks during boot, you are on a strapping or boot-glitch pin.
 * Move to GPIO 26. See the avoid table in docs/02-hardware-and-wiring.md.
 */

#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>

static const char *TAG = "feeder/main";

void app_main(void) {
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "SmartAqua feeder node (ESP32)");
    ESP_LOGI(TAG, "cores=%d  silicon rev=v%d.%d", chip.cores, chip.revision / 100, chip.revision % 100);
    ESP_LOGI(TAG, "free heap=%" PRIu32 " B", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Scaffolding only — no relay, schedule, or link yet.");
    ESP_LOGI(TAG, "See docs/09-roadmap-and-milestones.md (M2).");

    /* No relay is configured yet, so there is nothing to fail safe. The moment
     * a relay GPIO exists, it must be driven to its OFF level as the very first
     * action in app_main, before anything else can fail. */
    vTaskDelay(portMAX_DELAY);
}
