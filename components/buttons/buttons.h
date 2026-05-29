/**
 * @file buttons.h
 * @brief Four-button driver (Start, Stop, Reset, Mode) with event queue
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Button event types
 */
typedef enum {
    BTN_EVENT_START_PRESS,          /* Start button pressed */
    BTN_EVENT_STOP_PRESS,           /* Stop button pressed */
    BTN_EVENT_RESET_SHORT_PRESS,    /* Reset button short press (normal) */
    BTN_EVENT_RESET_LONG_PRESS,     /* Reset button held for >1 second (safety override) */
    BTN_EVENT_MODE_PRESS,           /* Mode button pressed */
} button_event_t;

/**
 * @brief Initialize button GPIOs and event queue
 * @return ESP_OK on success
 */
esp_err_t buttons_init(void);

/**
 * @brief Get a button event from queue (non-blocking)
 * @param event Pointer to store the event type
 * @return true if an event was available, false if queue empty
 */
bool buttons_get_event(button_event_t *event);

/**
 * @brief Flush all pending button events
 */
void buttons_flush_events(void);

#ifdef __cplusplus
}
#endif