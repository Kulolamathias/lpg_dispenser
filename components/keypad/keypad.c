/**
 * @file keypad.c
 * @brief 4x4 matrix keypad – internal pull‑up on columns
 */

#include "keypad.h"
#include "config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "KEYPAD";

// Key mapping (rows 0-3, cols 0-3)
static const char s_keymap[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

static const int row_gpios[4] = KEYPAD_ROW_GPIO;
static const int col_gpios[4] = KEYPAD_COL_GPIO;

static QueueHandle_t s_key_queue = NULL;

// Debounce
static char s_last_stable_key = 0;
static uint8_t s_debounce_counter = 0;

static void keypad_scan_task(void *pvParameters);

esp_err_t keypad_init(void) {
    // Configure rows as outputs, initially HIGH
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    for (int i = 0; i < 4; i++) {
        io_conf.pin_bit_mask = (1ULL << row_gpios[i]);
        gpio_config(&io_conf);
        gpio_set_level(row_gpios[i], 1);
    }

    // Configure columns as inputs with INTERNAL PULL-UP (critical fix)
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;   // <-- PULL HIGH
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    for (int i = 0; i < 4; i++) {
        io_conf.pin_bit_mask = (1ULL << col_gpios[i]);
        gpio_config(&io_conf);
    }

    s_key_queue = xQueueCreate(20, sizeof(char));
    if (!s_key_queue) {
        ESP_LOGE(TAG, "Failed to create key queue");
        return ESP_ERR_NO_MEM;
    }

    xTaskCreate(keypad_scan_task, "keypad_scan", 2048, NULL, 2, NULL);
    ESP_LOGI(TAG, "Keypad initialized (internal pull-up on columns)");
    return ESP_OK;
}

bool keypad_get_key(char *key) {
    if (!s_key_queue) return false;
    return xQueueReceive(s_key_queue, key, 0) == pdTRUE;
}

void keypad_flush_queue(void) {
    if (s_key_queue) xQueueReset(s_key_queue);
}

static char keypad_scan_once(void) {
    for (int row = 0; row < 4; row++) {
        // Drive all rows high, then drive current row low
        for (int r = 0; r < 4; r++) {
            gpio_set_level(row_gpios[r], 1);
        }
        gpio_set_level(row_gpios[row], 0);
        esp_rom_delay_us(50);  // short settle time (no need for task delay)

        // Read columns – low means pressed (because column is pulled high)
        for (int col = 0; col < 4; col++) {
            if (gpio_get_level(col_gpios[col]) == 0) {
                return s_keymap[row][col];
            }
        }
    }
    return 0;
}

static void keypad_scan_task(void *pvParameters) {
    const TickType_t scan_interval = pdMS_TO_TICKS(50);
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last_wake, scan_interval);
        char current_key = keypad_scan_once();

        if (current_key == s_last_stable_key) {
            if (s_debounce_counter < 2) s_debounce_counter++;
            if (s_debounce_counter == 2 && current_key != 0) {
                xQueueSend(s_key_queue, &current_key, 0);
                s_debounce_counter = 3; // prevent repeats while held
            }
        } else {
            s_last_stable_key = current_key;
            s_debounce_counter = 0;
        }
    }
}