idf_component_register(
    SRCS "lcd_i2c.c"
    INCLUDE_DIRS "."
    REQUIRES common_utils esp_timer driver
)