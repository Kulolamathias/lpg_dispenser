/**
 * @file relay.h
 * @brief Solenoid valve control (GPIO relay)
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize relay GPIO
 * @return ESP_OK on success
 */
esp_err_t relay_init(void);

/**
 * @brief Turn solenoid valve ON (open)
 */
void relay_on(void);

/**
 * @brief Turn solenoid valve OFF (closed)
 */
void relay_off(void);

/**
 * @brief Toggle solenoid valve state
 */
void relay_toggle(void);

/**
 * @brief Get current relay state
 * @return true if ON, false if OFF
 */
bool relay_is_on(void);

#ifdef __cplusplus
}
#endif