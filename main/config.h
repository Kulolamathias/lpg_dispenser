// config.h
// LPG Dispenser - Pin assignments, thresholds, and configurable macros
// All values are scalable for future WiFi/MQTT integration

#pragma once

// ==================== Load Cell (HX711) ====================
#define LOADCELL_DT_GPIO           16
#define LOADCELL_SCK_GPIO          17
#define LOADCELL_MAX_KG            20.0f

// Default calibration (will be overwritten by NVS after first calibration)
#define LOADCELL_DEFAULT_ZERO      0.0f      // raw value at zero kg
#define LOADCELL_DEFAULT_SLOPE     1.0f      // kg per raw unit (placeholder)

// ==================== LCD (I2C) ====================
#define LCD_I2C_ADDR               0x27      // Common address, try 0x3F if not working
#define LCD_I2C_SDA_GPIO           21
#define LCD_I2C_SCL_GPIO           22
#define LCD_COLS                   20
#define LCD_ROWS                   4

// ==================== Keypad 4x4 ====================
// Row GPIOs (outputs)
#define KEYPAD_ROW_GPIO            {25, 26, 27, 14}
// Column GPIOs (inputs with external pulldown)
#define KEYPAD_COL_GPIO            {32, 33, 34, 35}

// Keymap (rows 0-3, cols 0-3) - defined in keypad.c
// Special keys:
#define KEYPAD_ENTER_KEY           '#'
#define KEYPAD_CLEAR_KEY           '*'

// ==================== Buttons ====================
#define BTN_START_GPIO             18
#define BTN_STOP_GPIO              19
#define BTN_RESET_GPIO             23
#define BTN_MODE_GPIO              4

#define BTN_DEBOUNCE_MS            50
#define BTN_RESET_LONG_PRESS_MS    1000      // Hold reset for 1 second to force reset in safety state

// ==================== Relay (Solenoid Valve) ====================
#define RELAY_GPIO                 13

// Active high: 1 = turn on, 0 = turn off
// Active low:  1 = turn off, 0 = turn on
#define RELAY_ACTIVE_HIGH          1

#if RELAY_ACTIVE_HIGH
    #define RELAY_ON_STATE         1
    #define RELAY_OFF_STATE        0
#else
    #define RELAY_ON_STATE         0
    #define RELAY_OFF_STATE        1
#endif

// ==================== Safety Monitoring ====================
#define SAFETY_MASS_DROP_THRESHOLD_GRAMS   200   // Sudden drop >200g triggers safety stop
#define SAFETY_SAMPLE_INTERVAL_MS          50    // Check load cell every 50ms when dispensing
#define SAFETY_MIN_MASS_TO_MONITOR_GRAMS   200   // Ignore drops below this mass (no tank)

// ==================== Dispensing Defaults ====================
#define DEFAULT_PRICE_PER_KG       2500.0f   // Currency per kg (adjustable via calibration menu)
#define TARE_WEIGHT_DEFAULT_KG     12.0f     // Empty cylinder mass in kg (adjustable)

// ==================== NVS Storage Keys ====================
#define NVS_NAMESPACE              "lpg_cal"
#define NVS_KEY_ZERO               "zero"
#define NVS_KEY_SLOPE              "slope"
#define NVS_KEY_PRICE              "price"
#define NVS_KEY_TARE               "tare"

// ==================== Debug / UART ====================
// (Reserved for future logging, not used in main logic)