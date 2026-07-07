// config.h
// LPG Dispenser - Pin assignments, thresholds, and configurable macros
// All values are scalable for future WiFi/MQTT integration

#pragma once

// ==================== Load Cell (HX711) ====================
#define LOADCELL_DT_GPIO           5
#define LOADCELL_SCK_GPIO          6
#define LOADCELL_MAX_KG            20.0f
#define LOADCELL_DEFAULT_ZERO      0.0f
#define LOADCELL_BACKGROUND_SAMPLES 3
#define LOADCELL_BACKGROUND_INTERVAL_MS 20
#define LOADCELL_STALE_TIMEOUT_MS  2000

// Pre‑determined scale factor from calibration with 229g = 0.229 kg
// This gives kg per raw count (0.229 / delta_raw).
// Using the value I got from a successful calibration: 0.000436
#define LOADCELL_DEFAULT_SLOPE     0.000436f

// ==================== LCD (I2C) ====================
#define LCD_I2C_ADDR               0x27
#define LCD_I2C_SDA_GPIO           1
#define LCD_I2C_SCL_GPIO           2
#define LCD_COLS                   20
#define LCD_ROWS                   4
#define LCD_DYNAMIC_UPDATE_MS      250

// ==================== Keypad 4x4 ====================
#define KEYPAD_ROW_GPIO            {4, 39, 40, 7}
#define KEYPAD_COL_GPIO            {15, 16, 17, 18}
#define KEYPAD_ENTER_KEY           '#'
#define KEYPAD_CLEAR_KEY           '*'

// ==================== Buttons ====================
#define BTN_START_GPIO             8
#define BTN_STOP_GPIO              9
#define BTN_RESET_GPIO             10
#define BTN_MODE_GPIO              21
#define BTN_DEBOUNCE_MS            50
#define BTN_RESET_LONG_PRESS_MS    1000

// ==================== Relay ====================
#define RELAY_GPIO                 38
#define RELAY_ACTIVE_HIGH          1
#if RELAY_ACTIVE_HIGH
    #define RELAY_ON_STATE         1
    #define RELAY_OFF_STATE        0
#else
    #define RELAY_ON_STATE         0
    #define RELAY_OFF_STATE        1
#endif

// ==================== Safety ====================
#define SAFETY_MASS_DROP_THRESHOLD_GRAMS   3000   // Increased to avoid false triggers
#define SAFETY_SAMPLE_INTERVAL_MS          50
#define SAFETY_MIN_MASS_TO_MONITOR_GRAMS   200

// ==================== Dispensing ====================
#define DEFAULT_PRICE_PER_KG       2500.0f
#define TARE_WEIGHT_DEFAULT_KG     0.0f
#define DISPENSE_MASS_DECREASES    0
#define DISPENSE_STOP_TOLERANCE_KG 0.002f
#define DISPENSE_STOP_CONFIRM_COUNT 2
#define STATE_MACHINE_INTERVAL_MS  50
#define RELAY_SETTLE_MS            600

// ==================== NVS ====================
#define NVS_NAMESPACE              "lpg_cal"
#define NVS_KEY_ZERO               "zero"
#define NVS_KEY_SLOPE              "slope"
#define NVS_KEY_PRICE              "price"
#define NVS_KEY_TARE               "tare"
