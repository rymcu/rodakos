idf_component_register(
    SRCS
        "lib/decode.c"
        "lib/identify.c"
        "lib/quirc.c"
        "lib/version_db.c"
    INCLUDE_DIRS "lib")

# Same settings as the upstream ESP-IDF component: the ESP32-S3 FPU handles only single precision.
target_compile_definitions(${COMPONENT_LIB} PRIVATE QUIRC_FLOAT_TYPE=float QUIRC_USE_TGMATH)
