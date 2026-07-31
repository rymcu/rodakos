set(SOURCES
    "main.cc"
    "settings.cc"
    "usb_msc_mode.cc"
    "rodakos_adapters/audio_codec_input.cc"
    "rodakos_adapters/audio_codec_output.cc"
    "rodakos_adapters/board_device_adapter.cc"
    "rodakos_adapters/backlight_adapter.cc"
    "rodakos_adapters/camera_device.cc"
    "rodakos_adapters/qmi8658_motion_sensor.cc"
    "rodakos_adapters/wifi_adapter.cc"
    "rodakos_adapters/wifi_config.cc"
    "rodakos_adapters/file_service.cc"
    "phone_os/phone_app_registry.cc"
    "phone_os/phone_app_host.cc"
    "phone_os/phone_navigation.cc"
    "phone_os/phone_system.cc"
    "phone_os/phone_shell.cc"
    "phone_os/audio_focus_service.cc"
    "phone_os/audio_output_service.cc"
    "phone_os/audio_service.cc"
    "phone_os/music_player_service.cc"
    "phone_os/recording_service.cc"
    "phone_os/light_service.cc"
    "phone_os/motion_service.cc"
    "phone_os/button_binding_service.cc"
    "phone_os/camera_service.cc"
    "phone_os/time_service.cc"
    "phone_os/voice_audio_frontend.cc"
    "phone_os/voice_assistant_service.cc"
    "phone_os/voice_assistant_transport.cc"
    "phone_os/voice_recorder_service.cc"
    "phone_os/voice_wake_settings.cc"
    "phone_os/voice_wake_service.cc"
    "phone_os/wake_on_lan_packet.cc"
    "phone_os/wake_on_lan_service.cc"
    "phone_os/device_cloud_config.cc"
    "phone_os/ota_update_service.cc"
    "phone_os/unified_mqtt_service.cc"
    "phone_os/voice_cloud_websocket_transport.cc"
    "phone_os/web_file_system_service.cc"
    "phone_ui/phone_ui.cc"
    "phone_ui/phone_fonts.cc"
    "phone_ui/phone_theme.cc"
    "phone_ui/phone_screen.cc"
    "phone_ui/phone_components.cc"
    "phone_ui/rodakos_layout.cc"
    "phone_ui/rodakos_theme.cc"
    "phone_ui/soft_keyboard.cc"
    "phone_ui/image_library.cc"
    "phone_ui/jpg/jpeg_to_image.c"
    "apps/built_in_apps.cc"
    "apps/home/home_layout_model.cc"
    "apps/home/home_layout_codec.cc"
    "apps/home/home_layout_store.cc"
    "apps/home/home_app.cc"
    "apps/settings/settings_app.cc"
    "apps/settings/settings_app_internal.cc"
    "apps/settings/settings_buttons_page.cc"
    "apps/settings/settings_datetime_page.cc"
    "apps/settings/settings_device_cloud_page.cc"
    "apps/settings/settings_main_page.cc"
    "apps/settings/settings_system_shell_page.cc"
    "apps/settings/settings_usb_disk.cc"
    "apps/settings/settings_web_files_page.cc"
    "apps/settings/settings_wifi_page.cc"
    "apps/photos/photos_app.cc"
    "apps/camera/camera_app.cc"
    "apps/clock/clock_app.cc"
    "apps/calendar/calendar_app.cc"
    "apps/file_manager/file_manager_app.cc"
    "apps/gyro/gyro_app.cc"
    "apps/music/music_app.cc"
    "apps/recorder/recorder_app.cc"
    "apps/assistant/assistant_app.cc"
    "apps/system_info/system_info_app.cc"
    "apps/smart/smart_app.cc"
    "apps/wol/wol_device_codec.cc"
    "apps/wol/wol_device_store.cc"
    "apps/wol/wol_app.cc"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "." "rodakos_adapters" "phone_os" "phone_ui" "apps"
    REQUIRES
        lvgl
        esp_lvgl_port
        nvs_flash
        esp_wifi
        esp_http_server
        esp_http_client
        mqtt
        esp_netif
        lwip
        esp_board_manager
        esp_driver_i2c
        espressif__esp_websocket_client
        espressif__esp_codec_dev
        espressif__esp-sr
        78__esp-opus
        78__esp-opus-encoder
        chmorgan__esp-libhelix-mp3
        app_update
        bootloader_support
        esp_app_format
        esp_timer
        heap
        mbedtls
        rodak_ota_state
        led_strip
        spi_flash
        fatfs
        sdmmc
        esp_tinyusb
        esp_new_jpeg
)

# ESP-SR normally flashes models into a dedicated partition. RodakOS has an
# immutable Recovery layout, so generate the selected MultiNet bundle and embed
# it in ota_0 instead of changing the partition table.
idf_component_get_property(ESP_SR_COMPONENT_DIR espressif__esp-sr COMPONENT_DIR)
idf_component_get_property(ESP_DSP_COMPONENT_LIB espressif__esp-dsp COMPONENT_LIB)
idf_build_get_property(RODAKOS_BUILD_DIR BUILD_DIR)
set(RODAKOS_SR_MODELS_BIN "${RODAKOS_BUILD_DIR}/srmodels/srmodels.bin")
file(GLOB_RECURSE RODAKOS_SR_MODEL_INPUTS CONFIGURE_DEPENDS
    "${ESP_SR_COMPONENT_DIR}/model/*"
)

# ESP-DSP 1.6.0 relies on transitive math declarations that GCC 15 no longer
# places in std::. Keep the managed component untouched and inject <cmath>.
target_compile_options(${ESP_DSP_COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:cmath>
)

# ESP-SR 2.2.x is built against newlib and its prebuilt FST library references
# the legacy byte-table symbol. IDF 6 uses picolibc, where the same table base
# is exposed as `_ctype_b + 127` but the `_ctype_` symbol is no longer emitted.
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    idf_build_set_property(
        LINK_OPTIONS
        "-Wl,--defsym=_ctype_=_ctype_b+127"
        APPEND
    )
endif()

add_custom_command(
    OUTPUT "${RODAKOS_SR_MODELS_BIN}"
    COMMAND "${PYTHON}" "${ESP_SR_COMPONENT_DIR}/model/movemodel.py"
            -d1 "${SDKCONFIG}"
            -d2 "${ESP_SR_COMPONENT_DIR}"
            -d3 "${RODAKOS_BUILD_DIR}"
    DEPENDS "${SDKCONFIG}" ${RODAKOS_SR_MODEL_INPUTS}
    COMMENT "Pack embedded RodakOS voice models"
    VERBATIM
)

target_add_binary_data(
    ${COMPONENT_LIB}
    "${RODAKOS_SR_MODELS_BIN}"
    BINARY
    RENAME_TO rodakos_voice_models
    DEPENDS "${SDKCONFIG}" ${RODAKOS_SR_MODEL_INPUTS}
)
