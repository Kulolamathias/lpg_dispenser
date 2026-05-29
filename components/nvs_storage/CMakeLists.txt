idf_component_register(
    SRCS "nvs_storage.c"
    INCLUDE_DIRS "."
    REQUIRES common_utils nvs_flash
)