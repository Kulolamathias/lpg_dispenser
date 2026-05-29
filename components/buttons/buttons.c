/**
 * @file buttons.c
 * @brief Button driver with separate queues for raw interrupts and processed events
 */

#include "buttons.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "BUTTONS";

static const int button_pins[4] = {
    BTN_START_GPIO,
    BTN_STOP_GPIO,
    BTN_RESET_GPIO,
    BTN_MODE_GPIO
};

static const button_event_t press_events[4] = {
    BTN_EVENT_START_PRESS,
    BTN_EVENT_STOP_PRESS,
    BTN_EVENT_RESET_SHORT_PRESS,
    BTN_EVENT_MODE_PRESS
};

static QueueHandle_t s_irq_queue = NULL;      // raw GPIO numbers from ISR
static QueueHandle_t s_event_queue = NULL;    // processed button_event_t
static esp_timer_handle_t s_long_press_timer = NULL;
static volatile bool s_reset_pressed = false;

static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_irq_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

static void long_press_timer_cb(void *arg) {
    if (gpio_get_level(BTN_RESET_GPIO) == 0) {
        button_event_t event = BTN_EVENT_RESET_LONG_PRESS;
        xQueueSend(s_event_queue, &event, 0);
        ESP_LOGI(TAG, "Reset long press");
    }
    s_reset_pressed = false;
}

static void button_process_task(void *pvParameters) {
    uint32_t gpio_num;
    uint64_t press_time;

    while (1) {
        if (xQueueReceive(s_irq_queue, &gpio_num, portMAX_DELAY) == pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));
            if (gpio_get_level(gpio_num) == 0) {
                press_time = esp_timer_get_time();
                if (gpio_num == BTN_RESET_GPIO && !s_reset_pressed) {
                    s_reset_pressed = true;
                    esp_timer_start_once(s_long_press_timer, BTN_RESET_LONG_PRESS_MS * 1000);
                }
                while (gpio_get_level(gpio_num) == 0) vTaskDelay(pdMS_TO_TICKS(10));
                uint64_t duration_ms = (esp_timer_get_time() - press_time) / 1000;

                if (gpio_num == BTN_RESET_GPIO) {
                    esp_timer_stop(s_long_press_timer);
                    s_reset_pressed = false;
                    if (duration_ms < BTN_RESET_LONG_PRESS_MS) {
                        button_event_t ev = BTN_EVENT_RESET_SHORT_PRESS;
                        xQueueSend(s_event_queue, &ev, 0);
                    }
                } else {
                    for (int i = 0; i < 4; i++) {
                        if (button_pins[i] == gpio_num) {
                            xQueueSend(s_event_queue, &press_events[i], 0);
                            break;
                        }
                    }
                }
            }
        }
    }
}

esp_err_t buttons_init(void) {
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    for (int i = 0; i < 4; i++) {
        io_conf.pin_bit_mask = (1ULL << button_pins[i]);
        gpio_config(&io_conf);
    }

    s_irq_queue = xQueueCreate(10, sizeof(uint32_t));
    s_event_queue = xQueueCreate(10, sizeof(button_event_t));
    if (!s_irq_queue || !s_event_queue) return ESP_ERR_NO_MEM;

    esp_timer_create_args_t timer_args = {
        .callback = long_press_timer_cb,
        .name = "reset_long_press"
    };
    esp_timer_create(&timer_args, &s_long_press_timer);

    gpio_install_isr_service(0);
    for (int i = 0; i < 4; i++) {
        gpio_isr_handler_add(button_pins[i], button_isr_handler, (void*)(uint32_t)button_pins[i]);
    }

    xTaskCreate(button_process_task, "button_process", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Buttons initialized");
    return ESP_OK;
}

bool buttons_get_event(button_event_t *event) {
    if (!s_event_queue) return false;
    return xQueueReceive(s_event_queue, event, 0) == pdTRUE;
}

void buttons_flush_events(void) {
    if (s_event_queue) xQueueReset(s_event_queue);
}