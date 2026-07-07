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
#include <stdarg.h>
#include <stdlib.h>
#include "esp_err.h"
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
static char s_calib_buffer[16] = {0};
static uint8_t s_calib_len = 0;
static char s_last_lines[LCD_ROWS][LCD_COLS + 1] = {{0}};
static bool s_screen_valid = false;
static system_state_t s_last_state = (system_state_t)-1;
static calib_step_t s_last_calib_step = CALIB_NONE;

// Forward declaration
static void display_manager_calibration_lines(char lines[LCD_ROWS][LCD_COLS + 1]);

static void format_line(char dest[LCD_COLS + 1], const char *fmt, ...) {
    char buffer[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    memset(dest, ' ', LCD_COLS);
    size_t len = strlen(buffer);
    if (len > LCD_COLS) len = LCD_COLS;
    memcpy(dest, buffer, len);
    dest[LCD_COLS] = '\0';
}

static void write_lines(system_state_t state, char lines[LCD_ROWS][LCD_COLS + 1]) {
    if (!s_lcd) return;

    if (!s_screen_valid || state != s_last_state ||
        (state == STATE_CALIBRATION && s_calib_step != s_last_calib_step)) {
        lcd_clear(s_lcd);
        memset(s_last_lines, 0, sizeof(s_last_lines));
        s_screen_valid = false;
        s_last_state = state;
        s_last_calib_step = s_calib_step;
    }

    for (uint8_t row = 0; row < LCD_ROWS; row++) {
        if (!s_screen_valid || memcmp(s_last_lines[row], lines[row], LCD_COLS) != 0) {
            lcd_set_cursor(s_lcd, row, 0);
            lcd_print_str(s_lcd, lines[row]);
            memcpy(s_last_lines[row], lines[row], LCD_COLS + 1);
        }
    }
    s_screen_valid = true;
}

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
    memset(s_last_lines, 0, sizeof(s_last_lines));
    s_screen_valid = false;
    ESP_LOGI(TAG, "Display manager initialized");
    return ESP_OK;
}

void display_manager_update(system_state_t state, float empty_mass, float paid, 
                            float dispensed, float total_weight, 
                            const char *price_buffer, uint8_t price_len) {
    if (!s_lcd) return;
    (void)price_len;
    char lines[LCD_ROWS][LCD_COLS + 1];

    switch (state) {
        case STATE_IDLE:
            format_line(lines[0], "LPG Dispenser Ready");
            format_line(lines[1], "Press Start for TSHs");
            format_line(lines[2], "Price/kg: %.2f", loadcell_get_price_per_kg());
            format_line(lines[3], "Mode=Calibration");
            break;

        case STATE_PRICE_ENTRY:
            format_line(lines[0], "Enter Amount:");
            format_line(lines[1], "TSHs %s", price_buffer);
            format_line(lines[2], "Press # to confirm");
            format_line(lines[3], "* to clear");
            break;

        case STATE_READY:
            format_line(lines[0], "Paid: TSHs %.2f", paid);
            format_line(lines[1], "Target: %.2f kg", paid / loadcell_get_price_per_kg());
            format_line(lines[2], "Press Start");
            format_line(lines[3], "Stop/Pause Reset");
            break;

        case STATE_DISPENSING:
        case STATE_PAUSED:
            format_line(lines[0], "Empty: %.2f kg", empty_mass);
            format_line(lines[1], "Paid: TSHs %.2f", paid);
            format_line(lines[2], "Disp: %.3f/%.3fkg", dispensed, paid / loadcell_get_price_per_kg());
            if (state == STATE_PAUSED) {
                format_line(lines[3], "Total: %.2fkg PAUSED", total_weight);
            } else {
                format_line(lines[3], "Total: %.2f kg", total_weight);
            }
            break;

        case STATE_SAFETY_STOP:
            format_line(lines[0], "!!! SAFETY STOP !!!");
            format_line(lines[1], "Mass/drop or sensor");
            format_line(lines[2], "Press Reset");
            format_line(lines[3], "to continue");
            break;

        case STATE_CALIBRATION:
            display_manager_calibration_lines(lines);
            break;

        default:
            format_line(lines[0], "");
            format_line(lines[1], "");
            format_line(lines[2], "");
            format_line(lines[3], "");
            break;
    }
    write_lines(state, lines);
}

static void display_manager_calibration_lines(char lines[LCD_ROWS][LCD_COLS + 1]) {
    switch (s_calib_step) {
        case CALIB_ZERO:
            format_line(lines[0], "Calib: Zero Offset");
            format_line(lines[1], "Remove all weight");
            format_line(lines[2], "Press # set zero");
            format_line(lines[3], "Current: %.2f kg", loadcell_get_latest_kg());
            break;
        case CALIB_KNOWN_MASS:
            format_line(lines[0], "Calib: Known Mass");
            format_line(lines[1], "Mass(g): %s", s_calib_buffer);
            format_line(lines[2], "Place mass & #");
            format_line(lines[3], "* clear, D=skip");
            break;
        case CALIB_PRICE:
            format_line(lines[0], "Set Price per kg");
            format_line(lines[1], "Current: %.2f", loadcell_get_price_per_kg());
            format_line(lines[2], "New: %s", s_calib_buffer);
            format_line(lines[3], "# save * clear D");
            break;
        case CALIB_SAVE:
            format_line(lines[0], "Save calibration?");
            format_line(lines[1], "# = Yes  * = No");
            format_line(lines[2], "");
            format_line(lines[3], "");
            break;
        default:
            format_line(lines[0], "Calibration Menu");
            format_line(lines[1], "# Start");
            format_line(lines[2], "D to exit");
            format_line(lines[3], "");
            break;
    }
}

void display_manager_calibration_key(char key) {
    if (s_calib_step == CALIB_NONE) {
        if (key == '#') {
            s_calib_step = CALIB_ZERO;
        } else if (key == 'D' || key == 'd') {
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
            float grams = atof(s_calib_buffer);
            if (grams > 0) {
                float kg = grams / 1000.0f;          // Convert grams to kilograms
                loadcell_calibrate(kg);
                ESP_LOGI(TAG, "Calibrated with %.0f g (%.3f kg)", grams, kg);
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
            esp_err_t err = loadcell_save_calibration();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Calibration saved to NVS");
            } else {
                ESP_LOGE(TAG, "Calibration save failed: %s", esp_err_to_name(err));
            }
            s_calib_step = CALIB_NONE;
            statemachine_set_state(STATE_IDLE);
        } else if (key == '*') {
            s_calib_step = CALIB_NONE;
            statemachine_set_state(STATE_IDLE);
        }
    }
}
