/**
 * @file statemachine.c
 * @brief State machine implementation
 */

#include "state_machine.h"
#include "config.h"
#include "loadcell.h"
#include "keypad.h"
#include "buttons.h"
#include "relay.h"
#include "safety.h"
#include "display_manager.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "STATEMACHINE";

static system_state_t s_current_state = STATE_IDLE;
static system_state_t s_previous_state = STATE_IDLE;   // track previous state for resume logic
static TaskHandle_t s_state_task_handle = NULL;

// Dispensing session variables
static float s_paid_amount = 0.0f;        // currency entered by user
static float s_price_per_kg = 0.0f;       // loaded from loadcell
static float s_target_kg = 0.0f;          // gas to dispense = paid / price
static float s_initial_mass_kg = 0.0f;    // total mass when dispensing started
static float s_already_dispensed_kg = 0.0f; // for pause/resume

// Price entry buffer (digits)
static char s_price_buffer[16] = {0};
static uint8_t s_price_len = 0;

// Forward declarations
static void state_idle_handler(void);
static void state_price_entry_handler(void);
static void state_ready_handler(void);
static void state_dispensing_handler(void);
static void state_paused_handler(void);
static void state_safety_stop_handler(void);
static void state_calibration_handler(void);
void statemachine_task(void *pvParameters);

static void process_buttons(void);
static void process_keypad(void);
static void update_display(void);

esp_err_t statemachine_init(void) {
    // Load current price per kg from loadcell module
    s_price_per_kg = loadcell_get_price_per_kg();

    // Create state machine task (stack 4096, priority 2)
    BaseType_t res = xTaskCreate(statemachine_task, "statemachine", 4096, NULL, 2, &s_state_task_handle);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create state machine task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "State machine initialized");
    return ESP_OK;
}

system_state_t statemachine_get_state(void) {
    return s_current_state;
}

void statemachine_set_state(system_state_t new_state) {
    if (new_state == s_current_state) return;
    s_previous_state = s_current_state;
    ESP_LOGI(TAG, "State transition: %d -> %d", s_current_state, new_state);
    s_current_state = new_state;

    // Perform state entry actions
    switch (new_state) {
        case STATE_IDLE:
            relay_off();
            safety_set_monitoring(false);
            safety_clear_trigger();
            break;
        case STATE_PRICE_ENTRY:
            memset(s_price_buffer, 0, sizeof(s_price_buffer));
            s_price_len = 0;
            break;
        case STATE_READY:
            // Already have s_paid_amount and s_target_kg from price entry
            break;
        case STATE_DISPENSING:
            if (s_previous_state == STATE_PAUSED) {
                // Resume: new initial mass, keep already dispensed
                s_initial_mass_kg = loadcell_read_kg();
            } else {
                s_initial_mass_kg = loadcell_read_kg();
                s_already_dispensed_kg = 0.0f;
            }
            relay_on();
            safety_set_monitoring(true);
            break;
        case STATE_PAUSED:
            relay_off();
            safety_set_monitoring(false);
            break;
        case STATE_SAFETY_STOP:
            relay_off();
            safety_set_monitoring(false);
            break;
        case STATE_CALIBRATION:
            relay_off();
            safety_set_monitoring(false);
            break;
    }
}

static void process_buttons(void) {
    button_event_t ev;
    while (buttons_get_event(&ev)) {
        switch (ev) {
            case BTN_EVENT_START_PRESS:
                if (s_current_state == STATE_READY) {
                    statemachine_set_state(STATE_DISPENSING);
                } else if (s_current_state == STATE_PAUSED) {
                    statemachine_set_state(STATE_DISPENSING);
                }
                break;

            case BTN_EVENT_STOP_PRESS:
                if (s_current_state == STATE_DISPENSING) {
                    statemachine_set_state(STATE_PAUSED);
                }
                break;

            case BTN_EVENT_RESET_SHORT_PRESS:
                if (s_current_state == STATE_SAFETY_STOP) {
                    statemachine_set_state(STATE_IDLE);
                } else if (s_current_state != STATE_CALIBRATION) {
                    // Abort current session
                    statemachine_set_state(STATE_IDLE);
                }
                break;

            case BTN_EVENT_RESET_LONG_PRESS:
                // Force reset even in calibration or any state
                statemachine_set_state(STATE_IDLE);
                break;

            case BTN_EVENT_MODE_PRESS:
                if (s_current_state == STATE_IDLE) {
                    statemachine_set_state(STATE_CALIBRATION);
                } else if (s_current_state == STATE_CALIBRATION) {
                    statemachine_set_state(STATE_IDLE);
                }
                break;

            default:
                break;
        }
    }
}

static void process_keypad(void) {
    char key;
    while (keypad_get_key(&key)) {
        if (s_current_state == STATE_PRICE_ENTRY) {
            if (key >= '0' && key <= '9') {
                if (s_price_len < 15) {
                    s_price_buffer[s_price_len++] = key;
                }
            } else if (key == KEYPAD_ENTER_KEY) {
                // Convert buffer to float
                s_paid_amount = atof(s_price_buffer);
                if (s_paid_amount > 0 && s_price_per_kg > 0) {
                    s_target_kg = s_paid_amount / s_price_per_kg;
                    statemachine_set_state(STATE_READY);
                } else {
                    // Invalid entry: stay in price entry, show error on display
                    ESP_LOGW(TAG, "Invalid price entry: %s", s_price_buffer);
                    memset(s_price_buffer, 0, sizeof(s_price_buffer));
                    s_price_len = 0;
                }
            } else if (key == KEYPAD_CLEAR_KEY) {
                memset(s_price_buffer, 0, sizeof(s_price_buffer));
                s_price_len = 0;
            }
        } else if (s_current_state == STATE_CALIBRATION) {
            // Keys are handled by calibration menu (through display_manager)
            // Pass key to display_manager for processing
            display_manager_calibration_key(key);
        }
    }
}

static void state_idle_handler(void) {
    // Nothing to do, waiting for Mode (enter calibration) or Start (enter price)
    // We need a way to enter price entry from IDLE. We'll use Start button to go to PRICE_ENTRY.
    // static bool start_held = false;
    button_event_t ev;
    while (buttons_get_event(&ev)) {
        if (ev == BTN_EVENT_START_PRESS && s_current_state == STATE_IDLE) {
            statemachine_set_state(STATE_PRICE_ENTRY);
        }
        // Re-process other buttons (like Mode) normally; but we already have process_buttons.
        // To avoid duplication, we'll move the logic: process_buttons already handles Mode.
        // So we call process_buttons first? Better to restructure: call process_buttons() before state handlers.
    }
}

static void state_price_entry_handler(void) {
    // Already handled by process_keypad, which transitions to READY when Enter pressed.
    // Also handle Start? No, only keypad.
}

static void state_ready_handler(void) {
    // Display shows paid amount and target kg, waiting for Start.
    // Start button is handled in process_buttons.
}

static void state_dispensing_handler(void) {
    float current_mass = loadcell_read_kg();
    float dispensed_kg = (s_initial_mass_kg - current_mass) + s_already_dispensed_kg;
    if (dispensed_kg >= s_target_kg) {
        // Target reached
        statemachine_set_state(STATE_IDLE);
        ESP_LOGI(TAG, "Dispensing complete: %.2f / %.2f kg", dispensed_kg, s_target_kg);
        return;
    }

    // Check safety event
    if (safety_get_event()) {
        statemachine_set_state(STATE_SAFETY_STOP);
        return;
    }

    // Update display shows live dispensed amount (display manager will handle)
}

static void state_paused_handler(void) {
    // Store already dispensed amount when entering paused
    static bool stored = false;
    if (!stored) {
        float current_mass = loadcell_read_kg();
        s_already_dispensed_kg = (s_initial_mass_kg - current_mass);
        stored = true;
    }
    // When resuming, s_initial_mass_kg will be set again, and s_already_dispensed_kg added.
    // Actually we need to adjust resume logic: on resume, set new initial mass and keep already_dispensed.
    // We'll handle that in state transition to DISPENSING.
}

static void state_safety_stop_handler(void) {
    // Display shows error, only Reset button works (already handled)
}

static void state_calibration_handler(void) {
    // Calibration is driven by display_manager, which calls loadcell functions and saves.
    // Nothing else here.
}

// Main state machine task loop
void statemachine_task(void *pvParameters) {
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(100);

    while (1) {
        vTaskDelayUntil(&last_wake, interval);

        // Process inputs
        process_buttons();
        process_keypad();

        // Run state-specific actions
        switch (s_current_state) {
            case STATE_IDLE:
                state_idle_handler();
                break;
            case STATE_PRICE_ENTRY:
                state_price_entry_handler();
                break;
            case STATE_READY:
                state_ready_handler();
                break;
            case STATE_DISPENSING:
                state_dispensing_handler();
                break;
            case STATE_PAUSED:
                state_paused_handler();
                break;
            case STATE_SAFETY_STOP:
                state_safety_stop_handler();
                break;
            case STATE_CALIBRATION:
                state_calibration_handler();
                break;
        }

        // Update display (display manager reads current state and variables)
        update_display();
    }
}

static void update_display(void) {
    // Gather data for display manager
    float empty_mass = 12.0f;  // TODO: store tare weight in NVS
    float paid = s_paid_amount;
    float dispensed = 0.0f;
    float total_weight = loadcell_read_kg();

    if (s_current_state == STATE_DISPENSING || s_current_state == STATE_PAUSED) {
        float current_mass = loadcell_read_kg();
        dispensed = (s_initial_mass_kg - current_mass) + s_already_dispensed_kg;
        if (dispensed < 0) dispensed = 0;
    }

    display_manager_update(s_current_state, empty_mass, paid, dispensed, total_weight, s_price_buffer, s_price_len);
}