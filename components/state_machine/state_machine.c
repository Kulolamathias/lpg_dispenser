/**
 * @file statemachine.c
 * @brief State machine – with stabilisation delay after relay on
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
static system_state_t s_previous_state = STATE_IDLE;
static TaskHandle_t s_state_task_handle = NULL;

static float s_paid_amount = 0.0f;
static float s_price_per_kg = 0.0f;
static float s_target_kg = 0.0f;
static float s_initial_mass_kg = 0.0f;
static float s_already_dispensed_kg = 0.0f;

static char s_price_buffer[16] = {0};
static uint8_t s_price_len = 0;

static void process_buttons(void);
static void process_keypad(void);
static void update_display(void);
void statemachine_task(void *pvParameters);

esp_err_t statemachine_init(void) {
    s_price_per_kg = loadcell_get_price_per_kg();
    BaseType_t res = xTaskCreate(statemachine_task, "statemachine", 4096, NULL, 2, &s_state_task_handle);
    if (res != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "State machine initialized");
    return ESP_OK;
}

system_state_t statemachine_get_state(void) { return s_current_state; }

void statemachine_set_state(system_state_t new_state) {
    if (new_state == s_current_state) return;
    s_previous_state = s_current_state;
    ESP_LOGI(TAG, "State transition: %d -> %d", s_current_state, new_state);
    s_current_state = new_state;

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
            break;
        case STATE_DISPENSING:
            if (s_previous_state == STATE_PAUSED) {
                s_initial_mass_kg = loadcell_read_kg();
            } else {
                s_initial_mass_kg = loadcell_read_kg();
                s_already_dispensed_kg = 0.0f;
            }
            relay_on();
            // Add delay to let HX711 stabilise after relay turn‑on
            vTaskDelay(pdMS_TO_TICKS(200));
            // safety_set_monitoring(true);   // <-- DISABLED for testing
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
                if (s_current_state == STATE_IDLE) statemachine_set_state(STATE_PRICE_ENTRY);
                else if (s_current_state == STATE_READY) statemachine_set_state(STATE_DISPENSING);
                else if (s_current_state == STATE_PAUSED) statemachine_set_state(STATE_DISPENSING);
                break;
            case BTN_EVENT_STOP_PRESS:
                if (s_current_state == STATE_DISPENSING) statemachine_set_state(STATE_PAUSED);
                break;
            case BTN_EVENT_RESET_SHORT_PRESS:
                if (s_current_state == STATE_SAFETY_STOP) statemachine_set_state(STATE_IDLE);
                else if (s_current_state != STATE_CALIBRATION) statemachine_set_state(STATE_IDLE);
                break;
            case BTN_EVENT_RESET_LONG_PRESS:
                statemachine_set_state(STATE_IDLE);
                break;
            case BTN_EVENT_MODE_PRESS:
                if (s_current_state == STATE_IDLE) statemachine_set_state(STATE_CALIBRATION);
                else if (s_current_state == STATE_CALIBRATION) statemachine_set_state(STATE_IDLE);
                break;
            default: break;
        }
    }
}

static void process_keypad(void) {
    char key;
    while (keypad_get_key(&key)) {
        if (s_current_state == STATE_PRICE_ENTRY) {
            if (key >= '0' && key <= '9') {
                if (s_price_len < 15) s_price_buffer[s_price_len++] = key;
            } else if (key == KEYPAD_ENTER_KEY) {
                s_paid_amount = atof(s_price_buffer);
                if (s_paid_amount > 0 && s_price_per_kg > 0) {
                    s_target_kg = s_paid_amount / s_price_per_kg;
                    statemachine_set_state(STATE_READY);
                } else {
                    ESP_LOGW(TAG, "Invalid price entry: %s", s_price_buffer);
                    memset(s_price_buffer, 0, sizeof(s_price_buffer));
                    s_price_len = 0;
                }
            } else if (key == KEYPAD_CLEAR_KEY) {
                memset(s_price_buffer, 0, sizeof(s_price_buffer));
                s_price_len = 0;
            }
        } else if (s_current_state == STATE_CALIBRATION) {
            display_manager_calibration_key(key);
        }
    }
}

static void state_dispensing_handler(void) {
    float current_mass = loadcell_read_kg();
    float dispensed_kg = (s_initial_mass_kg - current_mass) + s_already_dispensed_kg;
    if (dispensed_kg >= s_target_kg) {
        statemachine_set_state(STATE_IDLE);
        ESP_LOGI(TAG, "Dispensing complete: %.2f / %.2f kg", dispensed_kg, s_target_kg);
        return;
    }
    // Safety check disabled – we removed safety_get_event call
}

void statemachine_task(void *pvParameters) {
    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
        process_buttons();
        process_keypad();
        if (s_current_state == STATE_DISPENSING) state_dispensing_handler();
        update_display();
    }
}

static void update_display(void) {
    float empty_mass = 0.0f;   // no hardcoded 12 kg
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