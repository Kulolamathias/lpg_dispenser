/**
 * @file loadcell.h
 * @brief Load cell (HX711) driver for LPG dispenser
 * @version 1.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize HX711 and load default calibration from NVS
 * @return ESP_OK on success
 */
esp_err_t loadcell_init(void);

/**
 * @brief Read current mass in kilograms (blocking, averaged)
 * @return mass in kg, or -1.0f on error
 */
float loadcell_read_kg(void);

/**
 * @brief Set current reading as zero offset (tare)
 * @return ESP_OK on success
 */
esp_err_t loadcell_set_zero(void);

/**
 * @brief Calibrate slope using a known mass
 * @param known_kg Known mass in kilograms (e.g., 5.0 kg)
 * @return ESP_OK on success
 * @note Call loadcell_set_zero() first before placing known mass
 */
esp_err_t loadcell_calibrate(float known_kg);

/**
 * @brief Get current price per kg (currency units)
 * @return price per kg
 */
float loadcell_get_price_per_kg(void);

/**
 * @brief Set price per kg (stored in RAM, not saved to NVS)
 * @param price Price per kg
 */
void loadcell_set_price_per_kg(float price);

/**
 * @brief Save current calibration (zero, slope) and price per kg to NVS
 * @return ESP_OK on success
 */
esp_err_t loadcell_save_calibration(void);

/**
 * @brief Load calibration and price from NVS (called automatically in init)
 * @return ESP_OK on success, or ESP_ERR_NVS_NOT_FOUND if first run
 */
esp_err_t loadcell_load_calibration(void);

/**
 * @brief Get raw 24-bit reading from HX711 (for debugging)
 * @return raw signed 32-bit value (24-bit left-aligned)
 */
int32_t loadcell_read_raw(void);

#ifdef __cplusplus
}
#endif