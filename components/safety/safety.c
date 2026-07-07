/**
 * @file safety.c
 * @brief Safety monitoring implementation
 */

#include "safety.h"
#include "config.h"
#include "loadcell.h"
#include "relay.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define SAFETY_TASK_STACK_SIZE 4096
#define SAFETY_TASK_PRIORITY   4

static const char *TAG = "SAFETY";

static TaskHandle_t s_safety_task_handle = NULL;
static bool s_monitoring_enabled = false;
static bool s_safety_triggered = false;

// Queue to communicate between safety task and main state machine
static QueueHandle_t s_safety_event_queue = NULL;

// Safety event types
typedef enum {
    SAFETY_EVENT_MASS_DROP
} safety_event_t;

// Forward declaration
static void safety_task(void *pvParameters);

esp_err_t safety_init(void) {
    // Create queue for safety events (to notify state machine)
    s_safety_event_queue = xQueueCreate(5, sizeof(safety_event_t));
    if (s_safety_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create safety event queue");
        return ESP_ERR_NO_MEM;
    }

    // Create safety monitoring task (high priority, stack 4096)
    BaseType_t res = xTaskCreate(safety_task, "safety_task", SAFETY_TASK_STACK_SIZE, NULL, SAFETY_TASK_PRIORITY, &s_safety_task_handle);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create safety task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Safety initialized (drop threshold: %d g, check interval: %d ms)",
             SAFETY_MASS_DROP_THRESHOLD_GRAMS, SAFETY_SAMPLE_INTERVAL_MS);
    return ESP_OK;
}

void safety_set_monitoring(bool enable) {
    s_monitoring_enabled = enable;
    if (!enable) {
        s_safety_triggered = false;
    }
    ESP_LOGD(TAG, "Monitoring %s", enable ? "ENABLED" : "DISABLED");
}

bool safety_is_triggered(void) {
    return s_safety_triggered;
}

void safety_clear_trigger(void) {
    s_safety_triggered = false;
    if (s_safety_event_queue) {
        xQueueReset(s_safety_event_queue);
    }
    ESP_LOGI(TAG, "Safety trigger cleared");
}

void safety_trigger(void) {
    if (s_monitoring_enabled && !s_safety_triggered) {
        s_safety_triggered = true;
        // Close solenoid immediately
        relay_off();
        // Notify state machine via queue
        safety_event_t event = SAFETY_EVENT_MASS_DROP;
        xQueueSend(s_safety_event_queue, &event, 0);
        ESP_LOGW(TAG, "SAFETY TRIGGERED - mass drop detected");
    }
}

// Called by state machine to check if safety event occurred
bool safety_get_event(void) {
    safety_event_t event;
    if (xQueueReceive(s_safety_event_queue, &event, 0) == pdTRUE) {
        return true;  // event occurred (only mass drop for now)
    }
    return false;
}

static void safety_task(void *pvParameters) {
    TickType_t last_wake = xTaskGetTickCount();
    float prev_mass = 0;
    bool prev_monitoring = false;

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SAFETY_SAMPLE_INTERVAL_MS));

        // If monitoring just enabled, reset baseline
        if (s_monitoring_enabled && !prev_monitoring) {
            prev_mass = loadcell_get_latest_kg() * 1000;  // convert to grams
        }

        if (s_monitoring_enabled && !s_safety_triggered) {
            if (!loadcell_has_fresh_reading(LOADCELL_STALE_TIMEOUT_MS)) {
                ESP_LOGW(TAG, "Load-cell reading stale during safety monitoring");
                safety_trigger();
                prev_monitoring = s_monitoring_enabled;
                continue;
            }

            float current_mass_g = loadcell_get_latest_kg() * 1000;

            // Only monitor if mass is above minimum threshold (tank present)
            if (current_mass_g > SAFETY_MIN_MASS_TO_MONITOR_GRAMS) {
                float drop = prev_mass - current_mass_g;
                if (drop > SAFETY_MASS_DROP_THRESHOLD_GRAMS) {
                    ESP_LOGW(TAG, "Mass drop detected: %.1f g > %d g", drop, SAFETY_MASS_DROP_THRESHOLD_GRAMS);
                    safety_trigger();
                }
            }

            // Update previous mass for next iteration (only if not a spike)
            if (current_mass_g > 0) {
                // Simple low-pass: 70% previous, 30% new to avoid noise spikes
                prev_mass = 0.7f * prev_mass + 0.3f * current_mass_g;
            }
        } else if (!s_monitoring_enabled) {
            prev_mass = 0;
        }

        prev_monitoring = s_monitoring_enabled;
    }
}
