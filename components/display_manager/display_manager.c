/**
 * @file display_manager.c
 * @brief Display manager implementation using your lcd_i2c library
 */

#include "display_manager.h"
#include "config.h"
#include "loadcell.h"
#include "lcd_i2c.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "DISPLAY_MGR";
static lcd_handle_t *s_lcd = NULL;

// Calibration sub-state
typedef enum {
    CALIB_NONE,
    CALIB_ZERO,
    CALIB_KNOWN_MASS,
    CALIB_PRICE,
    CALIB_SAVE
} calib_step_t;
static calib_step_t s_calib_step = CALIB_NONE;
static float s_calib_known_mass = 0.0f;
static char s_calib_buffer[16] = {0};
static uint8_t s_calib_len = 0;

// Forward declaration
static void display_manager_calibration_draw(void);

esp_err_t display_manager_init(void) {
    // Initialize I2C (if not already done by LCD driver)
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = LCD_I2C_SDA_GPIO,
        .scl_io_num = LCD_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);

    // Configure LCD
    lcd_config_t lcd_config = {
        .i2c_port = I2C_NUM_0,
        .i2c_addr = LCD_I2C_ADDR,
        .rows = LCD_ROWS,
        .cols = LCD_COLS,
        .backlight_enable = true,
        .i2c_timeout_ms = 100,
        .cmd_delay_us = 50,
    };
    s_lcd = lcd_i2c_init(&lcd_config);
    if (!s_lcd) {
        ESP_LOGE(TAG, "LCD init failed");
        return ESP_ERR_NOT_FOUND;
    }
    lcd_clear(s_lcd);
    ESP_LOGI(TAG, "Display manager initialized");
    return ESP_OK;
}

void display_manager_update(system_state_t state, float empty_mass, float paid, 
                            float dispensed, float total_weight, 
                            const char *price_buffer, uint8_t price_len) {
    if (!s_lcd) return;

    lcd_clear(s_lcd);

    switch (state) {
        case STATE_IDLE:
            lcd_set_cursor(s_lcd, 0, 0);
            lcd_print_str(s_lcd, "LPG Dispenser Ready");
            lcd_set_cursor(s_lcd, 1, 0);
            lcd_print_str(s_lcd, "Press Start for $");
            lcd_set_cursor(s_lcd, 2, 0);
            lcd_printf(s_lcd, "Price/kg: %.2f", loadcell_get_price_per_kg());
            lcd_set_cursor(s_lcd, 3, 0);
            lcd_print_str(s_lcd, "Mode=Calibration");
            break;

        case STATE_PRICE_ENTRY:
            lcd_set_cursor(s_lcd, 0, 0);
            lcd_print_str(s_lcd, "Enter Amount:");
            lcd_set_cursor(s_lcd, 1, 0);
            lcd_printf(s_lcd, "$ %s", price_buffer);
            lcd_set_cursor(s_lcd, 2, 0);
            lcd_print_str(s_lcd, "Press # to confirm");
            lcd_set_cursor(s_lcd, 3, 0);
            lcd_print_str(s_lcd, "* to clear");
            break;

        case STATE_READY:
            lcd_set_cursor(s_lcd, 0, 0);
            lcd_printf(s_lcd, "Paid: $ %.2f", paid);
            lcd_set_cursor(s_lcd, 1, 0);
            lcd_printf(s_lcd, "Target: %.2f kg", paid / loadcell_get_price_per_kg());
            lcd_set_cursor(s_lcd, 2, 0);
            lcd_print_str(s_lcd, "Press Start");
            lcd_set_cursor(s_lcd, 3, 0);
            lcd_print_str(s_lcd, "Stop/Pause, Reset=abort");
            break;

        case STATE_DISPENSING:
        case STATE_PAUSED:
            lcd_set_cursor(s_lcd, 0, 0);
            lcd_printf(s_lcd, "Empty: %.2f kg", empty_mass);
            lcd_set_cursor(s_lcd, 1, 0);
            lcd_printf(s_lcd, "Paid: $ %.2f", paid);
            lcd_set_cursor(s_lcd, 2, 0);
            lcd_printf(s_lcd, "Disp: %.2f / %.2f kg", dispensed, paid / loadcell_get_price_per_kg());
            lcd_set_cursor(s_lcd, 3, 0);
            lcd_printf(s_lcd, "Total: %.2f kg", total_weight);
            if (state == STATE_PAUSED) {
                lcd_set_cursor(s_lcd, 3, 15);
                lcd_print_str(s_lcd, "PAUSED");
            }
            break;

        case STATE_SAFETY_STOP:
            lcd_set_cursor(s_lcd, 0, 0);
            lcd_print_str(s_lcd, "!!! SAFETY STOP !!!");
            lcd_set_cursor(s_lcd, 1, 0);
            lcd_print_str(s_lcd, "Mass drop detected");
            lcd_set_cursor(s_lcd, 2, 0);
            lcd_print_str(s_lcd, "Press Reset");
            lcd_set_cursor(s_lcd, 3, 0);
            lcd_print_str(s_lcd, "to continue");
            break;

        case STATE_CALIBRATION:
            display_manager_calibration_draw();
            break;

        default:
            break;
    }
}

static void display_manager_calibration_draw(void) {
    lcd_clear(s_lcd);
    switch (s_calib_step) {
        case CALIB_ZERO:
            lcd_set_cursor(s_lcd, 0, 0);
            lcd_print_str(s_lcd, "Calib: Zero Offset");
            lcd_set_cursor(s_lcd, 1, 0);
            lcd_print_str(s_lcd, "Remove all weight");
            lcd_set_cursor(s_lcd, 2, 0);
            lcd_print_str(s_lcd, "Press # to set zero");
            lcd_set_cursor(s_lcd, 3, 0);
            lcd_printf(s_lcd, "Current: %.2f kg", loadcell_read_kg());
            break;
        case CALIB_KNOWN_MASS:
            lcd_set_cursor(s_lcd, 0, 0);
            lcd_print_str(s_lcd, "Calib: Known Mass");
            lcd_set_cursor(s_lcd, 1, 0);
            lcd_printf(s_lcd, "Enter mass (kg): %s", s_calib_buffer);
            lcd_set_cursor(s_lcd, 2, 0);
            lcd_print_str(s_lcd, "Place mass & #");
            lcd_set_cursor(s_lcd, 3, 0);
            lcd_print_str(s_lcd, "* clear, D=skip");
            break;
        case CALIB_PRICE:
            lcd_set_cursor(s_lcd, 0, 0);
            lcd_print_str(s_lcd, "Set Price per kg");
            lcd_set_cursor(s_lcd, 1, 0);
            lcd_printf(s_lcd, "Current: %.2f", loadcell_get_price_per_kg());
            lcd_set_cursor(s_lcd, 2, 0);
            lcd_printf(s_lcd, "New: %s", s_calib_buffer);
            lcd_set_cursor(s_lcd, 3, 0);
            lcd_print_str(s_lcd, "# save, * clear, D skip");
            break;
        case CALIB_SAVE:
            lcd_set_cursor(s_lcd, 0, 0);
            lcd_print_str(s_lcd, "Save calibration?");
            lcd_set_cursor(s_lcd, 1, 0);
            lcd_print_str(s_lcd, "# = Yes  * = No");
            break;
        default:
            lcd_set_cursor(s_lcd, 0, 0);
            lcd_print_str(s_lcd, "Calibration Menu");
            lcd_set_cursor(s_lcd, 1, 0);
            lcd_print_str(s_lcd, "# Start");
            lcd_set_cursor(s_lcd, 2, 0);
            lcd_print_str(s_lcd, "D to exit");
            break;
    }
}

void display_manager_calibration_key(char key) {
    if (s_calib_step == CALIB_NONE) {
        if (key == '#') {
            s_calib_step = CALIB_ZERO;
        } else if (key == 'D' || key == 'd') {
            // Exit calibration via state machine (handled by button)
            // But we can also trigger state change? For now, just clear.
            s_calib_step = CALIB_NONE;
            statemachine_set_state(STATE_IDLE);
        }
    } else if (s_calib_step == CALIB_ZERO) {
        if (key == '#') {
            loadcell_set_zero();
            s_calib_step = CALIB_KNOWN_MASS;
            memset(s_calib_buffer, 0, sizeof(s_calib_buffer));
            s_calib_len = 0;
        }
    } else if (s_calib_step == CALIB_KNOWN_MASS) {
        if (key >= '0' && key <= '9') {
            if (s_calib_len < 15) {
                s_calib_buffer[s_calib_len++] = key;
            }
        } else if (key == KEYPAD_ENTER_KEY) {
            s_calib_known_mass = atof(s_calib_buffer);
            if (s_calib_known_mass > 0) {
                loadcell_calibrate(s_calib_known_mass);
                s_calib_step = CALIB_PRICE;
                memset(s_calib_buffer, 0, sizeof(s_calib_buffer));
                s_calib_len = 0;
            }
        } else if (key == KEYPAD_CLEAR_KEY) {
            memset(s_calib_buffer, 0, sizeof(s_calib_buffer));
            s_calib_len = 0;
        } else if (key == 'D' || key == 'd') {
            s_calib_step = CALIB_PRICE;  // skip known mass
        }
    } else if (s_calib_step == CALIB_PRICE) {
        if (key >= '0' && key <= '9') {
            if (s_calib_len < 15) {
                s_calib_buffer[s_calib_len++] = key;
            }
        } else if (key == KEYPAD_ENTER_KEY) {
            float new_price = atof(s_calib_buffer);
            if (new_price > 0) {
                loadcell_set_price_per_kg(new_price);
            }
            s_calib_step = CALIB_SAVE;
        } else if (key == KEYPAD_CLEAR_KEY) {
            memset(s_calib_buffer, 0, sizeof(s_calib_buffer));
            s_calib_len = 0;
        } else if (key == 'D' || key == 'd') {
            s_calib_step = CALIB_SAVE;
        }
    } else if (s_calib_step == CALIB_SAVE) {
        if (key == '#') {
            loadcell_save_calibration();
            s_calib_step = CALIB_NONE;
            statemachine_set_state(STATE_IDLE);
        } else if (key == '*') {
            s_calib_step = CALIB_NONE;
            statemachine_set_state(STATE_IDLE);
        }
    }
}