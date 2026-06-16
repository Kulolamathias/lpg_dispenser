/**
 * @file main.c
 * @brief LPG Dispenser main entry point
 * @version 1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "loadcell.h"
#include "keypad.h"
#include "buttons.h"
#include "relay.h"
#include "safety.h"
#include "state_machine.h"
#include "display_manager.h"

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "LPG Dispenser Starting...");
    ESP_LOGI(TAG, "========================================");

    // 1. Initialize NVS (required for calibration storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corruption, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return;
    }

    // 2. Initialize display manager (LCD)
    if (display_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed, continuing anyway...");
    }

    // 3. Initialize load cell (loads calibration from NVS)
    if (loadcell_init() != ESP_OK) {
        ESP_LOGE(TAG, "Load cell init failed!");
    }

    // 4. Initialize keypad
    if (keypad_init() != ESP_OK) {
        ESP_LOGE(TAG, "Keypad init failed!");
    }

    // 5. Initialize buttons
    if (buttons_init() != ESP_OK) {
        ESP_LOGE(TAG, "Buttons init failed!");
    }

    // 6. Initialize relay (solenoid valve)
    if (relay_init() != ESP_OK) {
        ESP_LOGE(TAG, "Relay init failed!");
    }

    // 7. Initialize safety monitor (starts background task)
    if (safety_init() != ESP_OK) {
        ESP_LOGE(TAG, "Safety monitor init failed!");
    }

    // 8. Initialize state machine (starts main task)
    if (statemachine_init() != ESP_OK) {
        ESP_LOGE(TAG, "State machine init failed!");
    }

    ESP_LOGI(TAG, "All modules initialized successfully");
    ESP_LOGI(TAG, "System ready. Press Mode to calibrate, Start to dispense.");

    // Main task runs in statemachine, so app_main can just idle or delete itself
    // We'll keep it alive for monitoring but not needed.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000)); // just keep task alive
    }
}
