/**
 * @file relay.c
 * @brief Relay driver with configurable active level
 */

#include "relay.h"
#include "config.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "RELAY";
static bool s_relay_state = false;  // current logical state (true = ON)

esp_err_t relay_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure relay GPIO");
        return err;
    }
    // Start with relay OFF
    relay_off();
    ESP_LOGI(TAG, "Relay initialized (active %s)", RELAY_ACTIVE_HIGH ? "HIGH" : "LOW");
    return ESP_OK;
}

static void relay_set_level(bool logical_on) {
    int level = logical_on ? RELAY_ON_STATE : RELAY_OFF_STATE;
    gpio_set_level(RELAY_GPIO, level);
    s_relay_state = logical_on;
}

void relay_on(void) {
    relay_set_level(true);
    ESP_LOGD(TAG, "Relay ON");
}

void relay_off(void) {
    relay_set_level(false);
    ESP_LOGD(TAG, "Relay OFF");
}

void relay_toggle(void) {
    relay_set_level(!s_relay_state);
}

bool relay_is_on(void) {
    return s_relay_state;
}