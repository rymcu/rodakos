idf_component_register(
    SRCS "rodak_ota_state.cc"
    INCLUDE_DIRS "include"
    REQUIRES mbedtls nvs_flash
)
