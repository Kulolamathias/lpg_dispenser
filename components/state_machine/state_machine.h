/**
 * @file state_machine.h
 * @brief LPG dispenser state machine
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief System states
 */
typedef enum {
    STATE_IDLE,               /* Waiting for price entry or mode button */
    STATE_PRICE_ENTRY,        /* User entering amount on keypad */
    STATE_READY,              /* Price entered, waiting for Start */
    STATE_DISPENSING,         /* Gas flowing, monitoring mass */
    STATE_PAUSED,             /* Dispensing paused, can resume */
    STATE_SAFETY_STOP,        /* Emergency stop due to mass drop */
    STATE_CALIBRATION         /* Calibration menu active */
} system_state_t;

/**
 * @brief Initialize state machine and start main loop task
 * @return ESP_OK on success
 */
esp_err_t statemachine_init(void);

/**
 * @brief Get current system state
 * @return current state
 */
system_state_t statemachine_get_state(void);

/**
 * @brief Manually set state (use with caution, mainly for reset)
 * @param new_state Target state
 */
void statemachine_set_state(system_state_t new_state);

#ifdef __cplusplus
}
#endif