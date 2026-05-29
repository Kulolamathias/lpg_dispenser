idf_component_register(
    SRCS "state_machine.c"
    INCLUDE_DIRS "."
    REQUIRES 
        common_utils
        loadcell
        keypad
        relay
        safety
        buttons
        display_manager
)