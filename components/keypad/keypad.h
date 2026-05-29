/**
 * @file keypad.h
 * @brief 4x4 matrix keypad driver with queue support
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize keypad GPIOs and internal queue
 * @return ESP_OK on success
 */
esp_err_t keypad_init(void);

/**
 * @brief Get a key press from the queue (non-blocking)
 * @param key Pointer to store the character (e.g., '1', '#', '*', 'A'...)
 * @return true if a key was available, false if queue empty
 */
bool keypad_get_key(char *key);

/**
 * @brief Flush (clear) all pending keys from queue
 */
void keypad_flush_queue(void);

/**
 * @brief Start the keypad scanning task (called automatically by init)
 * @note This is an internal function, but exposed if manual control needed
 */
void keypad_start_scanning(void);

#ifdef __cplusplus
}
#endif