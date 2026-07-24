idf_component_register(
    SRCS "recovery_main.cc"
    INCLUDE_DIRS "."
    REQUIRES
        app_update
        bootloader_support
        esp_driver_sdmmc
        fatfs
        mbedtls
        nvs_flash
        rodak_ota_state
)
