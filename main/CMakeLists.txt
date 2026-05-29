idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        relay
        safety
        buttons
        keypad
        loadcell
        nvs_storage
        display_manager
        state_machine
        common_utils
)