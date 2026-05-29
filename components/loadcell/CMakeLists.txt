idf_component_register(
    SRCS "loadcell.c"
    INCLUDE_DIRS "."
    REQUIRES 
        common_utils
        # nvs_flash
        nvs_storage 
        esp_timer
        driver
)