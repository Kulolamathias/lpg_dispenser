/**
 * @file loadcell.c
 * @brief HX711 driver implementation
 */

#include "loadcell.h"
#include "config.h"
#include "nvs_storage.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static const char *TAG = "LOADCELL";

// Static variables for calibration
static float s_zero_offset = LOADCELL_DEFAULT_ZERO;   // raw value at zero mass
static float s_slope = LOADCELL_DEFAULT_SLOPE;        // kg per raw unit
static float s_price_per_kg = DEFAULT_PRICE_PER_KG;

// Helper: set HX711 SCK high/low
static inline void sck_high(void) {
    gpio_set_level(LOADCELL_SCK_GPIO, 1);
}

static inline void sck_low(void) {
    gpio_set_level(LOADCELL_SCK_GPIO, 0);
}

// Wait for DT to go low (HX711 ready)
static esp_err_t wait_for_dt(uint32_t timeout_ms) {
    uint32_t start = esp_timer_get_time() / 1000;
    while (gpio_get_level(LOADCELL_DT_GPIO) == 1) {
        if ((esp_timer_get_time() / 1000) - start > timeout_ms) {
            ESP_LOGE(TAG, "HX711 timeout");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

int32_t loadcell_read_raw(void) {
    // Wait for HX711 to be ready
    if (wait_for_dt(100) != ESP_OK) {
        return 0;
    }

    int32_t value = 0;
    // Read 24 bits, MSB first
    for (int i = 0; i < 24; i++) {
        sck_high();
        esp_rom_delay_us(1);
        value <<= 1;
        if (gpio_get_level(LOADCELL_DT_GPIO)) {
            value |= 1;
        }
        sck_low();
        esp_rom_delay_us(1);
    }

    // 25th clock pulse to set gain = 128 (default for HX711)
    sck_high();
    esp_rom_delay_us(1);
    sck_low();
    esp_rom_delay_us(1);

    // Convert from two's complement (24-bit) to 32-bit signed
    if (value & 0x800000) {
        value |= 0xFF000000;
    }

    return value;
}

float loadcell_read_kg(void) {
    // Read raw 5 times and average for stability
    int32_t sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += loadcell_read_raw();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    int32_t raw_avg = sum / 5;

    // Convert to kg: (raw - zero) * slope
    float kg = ((float)(raw_avg - s_zero_offset)) * s_slope;
    if (kg < 0) kg = 0;
    return kg;
}

esp_err_t loadcell_set_zero(void) {
    ESP_LOGI(TAG, "Setting zero offset...");
    // Take multiple readings and average
    int32_t sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += loadcell_read_raw();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_zero_offset = sum / 10.0f;
    ESP_LOGI(TAG, "Zero offset = %.1f", s_zero_offset);
    return ESP_OK;
}

esp_err_t loadcell_calibrate(float known_kg) {
    if (known_kg <= 0) {
        ESP_LOGE(TAG, "Invalid known mass: %.2f kg", known_kg);
        return ESP_ERR_INVALID_ARG;
    }

    // Zero must be already set. Read current raw value with known mass.
    int32_t sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += loadcell_read_raw();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    float raw_with_mass = sum / 10.0f;

    // slope = kg / (raw - zero)
    float delta_raw = raw_with_mass - s_zero_offset;
    if (fabs(delta_raw) < 1.0f) {
        ESP_LOGE(TAG, "Delta raw too small, check load cell wiring or zero offset");
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_slope = known_kg / delta_raw;
    ESP_LOGI(TAG, "Calibration: known_kg=%.2f, delta_raw=%.1f, slope=%f", known_kg, delta_raw, s_slope);
    return ESP_OK;
}

float loadcell_get_price_per_kg(void) {
    return s_price_per_kg;
}

void loadcell_set_price_per_kg(float price) {
    if (price > 0) {
        s_price_per_kg = price;
        ESP_LOGI(TAG, "Price per kg set to %.2f", price);
    }
}

esp_err_t loadcell_save_calibration(void) {
    esp_err_t err;
    err = nvs_save_float(NVS_KEY_ZERO, s_zero_offset);
    if (err != ESP_OK) return err;
    err = nvs_save_float(NVS_KEY_SLOPE, s_slope);
    if (err != ESP_OK) return err;
    err = nvs_save_float(NVS_KEY_PRICE, s_price_per_kg);
    return err;
}

esp_err_t loadcell_load_calibration(void) {
    float val;
    esp_err_t err;

    err = nvs_load_float(NVS_KEY_ZERO, &val, LOADCELL_DEFAULT_ZERO);
    if (err == ESP_OK) s_zero_offset = val;

    err = nvs_load_float(NVS_KEY_SLOPE, &val, LOADCELL_DEFAULT_SLOPE);
    if (err == ESP_OK) s_slope = val;

    err = nvs_load_float(NVS_KEY_PRICE, &val, DEFAULT_PRICE_PER_KG);
    if (err == ESP_OK) s_price_per_kg = val;

    ESP_LOGI(TAG, "Loaded: zero=%.1f, slope=%f, price=%.2f", s_zero_offset, s_slope, s_price_per_kg);
    return ESP_OK;
}

esp_err_t loadcell_init(void) {
    // Configure GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LOADCELL_DT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << LOADCELL_SCK_GPIO);
    io_conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io_conf);

    // Set SCK low initially
    sck_low();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Load stored calibration from NVS
    loadcell_load_calibration();

    ESP_LOGI(TAG, "HX711 initialized");
    return ESP_OK;
}
