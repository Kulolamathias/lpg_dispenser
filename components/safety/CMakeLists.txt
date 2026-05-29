idf_component_register(
    SRCS "safety.c"
    INCLUDE_DIRS "."
    REQUIRES common_utils loadcell relay
)