/**
 * @file display_manager.h
 * @brief High-level UI for LPG dispenser
 */

#pragma once

#include "state_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize display manager (initializes LCD)
 * @return ESP_OK on success
 */
esp_err_t display_manager_init(void);

/**
 * @brief Update LCD content based on current state
 * @param state Current system state
 * @param initial_mass Tank mass before the current fill session (kg)
 * @param paid Amount paid (currency)
 * @param dispensed Amount dispensed so far (kg)
 * @param total_weight Current total weight from load cell (kg)
 * @param price_buffer String buffer for price entry
 * @param price_len Length of price buffer
 */
void display_manager_update(system_state_t state, float initial_mass, float paid,
                            float dispensed, float total_weight, 
                            const char *price_buffer, uint8_t price_len);

/**
 * @brief Handle keypad input during calibration mode
 * @param key Character key pressed
 */
void display_manager_calibration_key(char key);

#ifdef __cplusplus
}
#endif
