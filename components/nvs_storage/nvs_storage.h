/**
 * @file nvs_storage.h
 * @brief Non-volatile storage wrapper for calibration data
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Save a float value to NVS
 * @param key Unique string key
 * @param value Float value to store
 * @return ESP_OK on success
 */
esp_err_t nvs_save_float(const char *key, float value);

/**
 * @brief Load a float value from NVS
 * @param key Unique string key
 * @param value Output pointer
 * @param default_val Default value if key not found
 * @return ESP_OK if loaded or default used (ESP_ERR_NVS_NOT_FOUND is suppressed)
 */
esp_err_t nvs_load_float(const char *key, float *value, float default_val);

/**
 * @brief Save an integer to NVS
 * @param key Unique string key
 * @param value Integer value
 * @return ESP_OK on success
 */
esp_err_t nvs_save_int(const char *key, int32_t value);

/**
 * @brief Load an integer from NVS
 * @param key Unique string key
 * @param value Output pointer
 * @param default_val Default if not found
 * @return ESP_OK on success or default used
 */
esp_err_t nvs_load_int(const char *key, int32_t *value, int32_t default_val);

#ifdef __cplusplus
}
#endif