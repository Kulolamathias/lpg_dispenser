/**
 * @file loadcell.c
 * @brief HX711 driver with reset and automatic recovery
 */

#include "loadcell.h"
#include "config.h"
#include "nvs_storage.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static const char *TAG = "LOADCELL";

static float s_zero_offset = LOADCELL_DEFAULT_ZERO;
static float s_slope = LOADCELL_DEFAULT_SLOPE;
static float s_price_per_kg = DEFAULT_PRICE_PER_KG;

static int32_t s_last_valid_raw = 0;
static uint32_t s_timeout_count = 0;
static SemaphoreHandle_t s_hx711_mutex = NULL;
static TaskHandle_t s_loadcell_task_handle = NULL;
static portMUX_TYPE s_cache_lock = portMUX_INITIALIZER_UNLOCKED;
static float s_cached_kg = 0.0f;
static uint32_t s_cached_update_ms = 0;
static bool s_cached_valid = false;

/* ---- Hardware helpers ---- */
static inline void sck_high(void) { gpio_set_level(LOADCELL_SCK_GPIO, 1); }
static inline void sck_low(void)  { gpio_set_level(LOADCELL_SCK_GPIO, 0); }
static inline uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

static void update_cached_kg(float kg) {
    portENTER_CRITICAL(&s_cache_lock);
    s_cached_kg = kg;
    s_cached_update_ms = now_ms();
    s_cached_valid = true;
    portEXIT_CRITICAL(&s_cache_lock);
}

/* ---- Reset HX711 by pulsing SCK 36 times ---- */
static void hx711_reset(void) {
    ESP_LOGW(TAG, "Resetting HX711 (pulse SCK)");
    for (int i = 0; i < 36; i++) {
        sck_high();
        esp_rom_delay_us(1);
        sck_low();
        esp_rom_delay_us(1);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    s_timeout_count = 0;
}

static esp_err_t wait_for_dt(uint32_t timeout_ms) {
    uint32_t start = esp_timer_get_time() / 1000;
    while (gpio_get_level(LOADCELL_DT_GPIO) == 1) {
        if ((esp_timer_get_time() / 1000) - start > timeout_ms) {
            s_timeout_count++;
            if (s_timeout_count > 20) {
                // Too many consecutive timeouts – reset HX711
                hx711_reset();
                s_timeout_count = 0;
            }
            // Only log every 10th timeout to avoid flooding
            if (s_timeout_count % 10 == 0) {
                ESP_LOGE(TAG, "HX711 timeout");
            }
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    s_timeout_count = 0;  // reset counter on success
    return ESP_OK;
}

/* ---- Raw read (single sample) with retry; caller must hold s_hx711_mutex ---- */
static int32_t loadcell_read_raw_unlocked(void) {
    for (int retry = 0; retry < 3; retry++) {
        if (wait_for_dt(500) == ESP_OK) {
            int32_t value = 0;
            for (int i = 0; i < 24; i++) {
                sck_high();
                esp_rom_delay_us(1);
                value <<= 1;
                if (gpio_get_level(LOADCELL_DT_GPIO)) value |= 1;
                sck_low();
                esp_rom_delay_us(1);
            }
            sck_high(); esp_rom_delay_us(1); sck_low(); esp_rom_delay_us(1);
            if (value & 0x800000) value |= 0xFF000000;
            return value;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return 0;
}

/* ---- Raw read (single sample) with retry ---- */
int32_t loadcell_read_raw(void) {
    if (!s_hx711_mutex || xSemaphoreTake(s_hx711_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return 0;
    }
    int32_t raw = loadcell_read_raw_unlocked();
    xSemaphoreGive(s_hx711_mutex);
    return raw;
}

/* ---- Comparison for qsort ---- */
static int cmp_int32(const void *a, const void *b) {
    int32_t ia = *(const int32_t*)a;
    int32_t ib = *(const int32_t*)b;
    return (ia > ib) - (ia < ib);
}

/* ---- Outlier-rejected mass reading; caller must hold s_hx711_mutex ---- */
static bool read_kg_samples_unlocked(uint8_t sample_count, uint8_t min_valid_count, float *kg_out) {
    if (sample_count > 30) sample_count = 30;
    int32_t samples[30];
    int count = 0;
    for (int i = 0; i < sample_count; i++) {
        int32_t raw = loadcell_read_raw_unlocked();
        if (raw != 0) samples[count++] = raw;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (count < min_valid_count) {
        ESP_LOGW(TAG, "Too few valid samples");
        return false;
    }

    qsort(samples, count, sizeof(int32_t), cmp_int32);

    int trim = (count >= 10) ? (count / 10) : 0;
    int start = trim;
    int end = count - trim;
    int64_t sum = 0;
    for (int i = start; i < end; i++) {
        sum += samples[i];
    }
    int32_t raw_avg = sum / (end - start);

    const int32_t SPIKE_THRESHOLD = 5000;
    if (s_last_valid_raw != 0 && abs(raw_avg - s_last_valid_raw) > SPIKE_THRESHOLD) {
        static uint32_t spike_count = 0;
        spike_count++;
        if (spike_count < 3) {
            raw_avg = s_last_valid_raw;
        } else {
            s_last_valid_raw = raw_avg;
            spike_count = 0;
        }
    } else {
        s_last_valid_raw = raw_avg;
    }

    float kg = ((float)(raw_avg - s_zero_offset)) * s_slope;
    if (kg < 0) kg = 0;

    static uint32_t dbg_cnt = 0;
    if (dbg_cnt++ % 20 == 0) {
        ESP_LOGI(TAG, "raw=%ld  zero=%.1f  slope=%f  kg=%.3f", raw_avg, s_zero_offset, s_slope, kg);
    }
    *kg_out = kg;
    return true;
}

static float read_kg_samples(uint8_t sample_count, uint8_t min_valid_count) {
    if (!s_hx711_mutex || xSemaphoreTake(s_hx711_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return loadcell_get_latest_kg();
    }
    float kg = 0.0f;
    bool ok = read_kg_samples_unlocked(sample_count, min_valid_count, &kg);
    xSemaphoreGive(s_hx711_mutex);
    if (ok) {
        update_cached_kg(kg);
    }
    return ok ? kg : loadcell_get_latest_kg();
}

/* ---- Outlier-rejected mass reading ---- */
float loadcell_read_kg(void) {
    return read_kg_samples(30, 10);
}

float loadcell_get_latest_kg(void) {
    float kg;
    portENTER_CRITICAL(&s_cache_lock);
    kg = s_cached_kg;
    portEXIT_CRITICAL(&s_cache_lock);
    return kg;
}

bool loadcell_has_fresh_reading(uint32_t max_age_ms) {
    bool valid;
    uint32_t update_ms;
    portENTER_CRITICAL(&s_cache_lock);
    valid = s_cached_valid;
    update_ms = s_cached_update_ms;
    portEXIT_CRITICAL(&s_cache_lock);
    return valid && ((uint32_t)(now_ms() - update_ms) <= max_age_ms);
}

static void loadcell_sample_task(void *pvParameters) {
    while (1) {
        (void)read_kg_samples(LOADCELL_BACKGROUND_SAMPLES, 2);
        vTaskDelay(pdMS_TO_TICKS(LOADCELL_BACKGROUND_INTERVAL_MS));
    }
}

/* ---- Zero calibration ---- */
esp_err_t loadcell_set_zero(void) {
    ESP_LOGI(TAG, "Setting zero offset...");
    if (!s_hx711_mutex || xSemaphoreTake(s_hx711_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    int64_t sum = 0;
    for (int i = 0; i < 30; i++) {
        sum += loadcell_read_raw_unlocked();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_zero_offset = sum / 30.0f;
    ESP_LOGI(TAG, "Zero offset = %.1f", s_zero_offset);
    s_last_valid_raw = (int32_t)s_zero_offset;
    xSemaphoreGive(s_hx711_mutex);
    update_cached_kg(0.0f);
    return ESP_OK;
}

/* ---- Scale calibration ---- */
esp_err_t loadcell_calibrate(float known_kg) {
    if (known_kg <= 0) {
        ESP_LOGE(TAG, "Invalid known mass: %.2f kg", known_kg);
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_hx711_mutex || xSemaphoreTake(s_hx711_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    int64_t sum = 0;
    for (int i = 0; i < 30; i++) {
        sum += loadcell_read_raw_unlocked();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    float raw_with_mass = sum / 30.0f;
    float delta_raw = raw_with_mass - s_zero_offset;
    if (delta_raw < 0) {
        ESP_LOGW(TAG, "Delta raw negative (%.1f) – taking absolute value", delta_raw);
        delta_raw = -delta_raw;
    }
    if (fabs(delta_raw) < 5.0f) {
        ESP_LOGE(TAG, "Delta raw too small (%.1f)", delta_raw);
        xSemaphoreGive(s_hx711_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_slope = known_kg / delta_raw;
    ESP_LOGI(TAG, "Calibration: known_kg=%.3f, delta_raw=%.1f, slope=%f", known_kg, delta_raw, s_slope);
    xSemaphoreGive(s_hx711_mutex);
    (void)read_kg_samples(LOADCELL_BACKGROUND_SAMPLES, 2);
    return ESP_OK;
}

/* ---- Price get/set ---- */
float loadcell_get_price_per_kg(void) { return s_price_per_kg; }
void loadcell_set_price_per_kg(float price) {
    if (price > 0) {
        s_price_per_kg = price;
        ESP_LOGI(TAG, "Price per kg set to %.2f", price);
    }
}

/* ---- NVS save/load ---- */
esp_err_t loadcell_save_calibration(void) {
    esp_err_t err;
    err = nvs_save_float(NVS_KEY_ZERO, s_zero_offset);
    if (err != ESP_OK) return err;
    err = nvs_save_float(NVS_KEY_SLOPE, s_slope);
    if (err != ESP_OK) return err;
    err = nvs_save_float(NVS_KEY_PRICE, s_price_per_kg);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Calibration saved: zero=%.1f, slope=%f, price=%.2f", s_zero_offset, s_slope, s_price_per_kg);
    return ESP_OK;
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

/* ---- Init ---- */
esp_err_t loadcell_init(void) {
    if (!s_hx711_mutex) {
        s_hx711_mutex = xSemaphoreCreateMutex();
        if (!s_hx711_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

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
    sck_low();
    vTaskDelay(pdMS_TO_TICKS(100));
    loadcell_load_calibration();
    s_last_valid_raw = 0;
    if (!s_loadcell_task_handle) {
        BaseType_t res = xTaskCreate(loadcell_sample_task, "loadcell_sample", 4096, NULL, 2, &s_loadcell_task_handle);
        if (res != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "HX711 initialized with slope=%.6f", s_slope);
    return ESP_OK;
}
