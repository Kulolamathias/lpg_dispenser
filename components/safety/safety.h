/**
 * @file safety.h
 * @brief Safety monitor for sudden mass drop during dispensing
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize safety monitoring task
 * @return ESP_OK on success
 */
esp_err_t safety_init(void);

/**
 * @brief Enable/disable safety monitoring
 * @param enable true to enable monitoring (call when entering DISPENSING state)
 * @note When disabled, any pending safety condition is cleared
 */
void safety_set_monitoring(bool enable);

/**
 * @brief Check if a safety stop has been triggered
 * @return true if safety stop active, false otherwise
 */
bool safety_is_triggered(void);

/**
 * @brief Clear safety trigger (call after reset)
 */
void safety_clear_trigger(void);

/**
 * @brief Manually trigger safety stop (for testing or emergency)
 */
void safety_trigger(void);

/**
 * @brief Check if a safety event has occurred (non-blocking)
 * @return true if a safety event is pending, false otherwise
 * @note This is used by the state machine to detect mass drop events.
 */
bool safety_get_event(void);

#ifdef __cplusplus
}
#endif