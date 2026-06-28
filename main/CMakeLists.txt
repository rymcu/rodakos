set(SOURCES
    "main.cc"
    "settings.cc"
    "rodakos_adapters/backlight_adapter.cc"
    "rodakos_adapters/wifi_adapter.cc"
    "rodakos_adapters/wifi_config.cc"
    "phone_os/phone_app_registry.cc"
    "phone_os/phone_app_host.cc"
    "phone_os/phone_navigation.cc"
    "phone_os/phone_system.cc"
    "phone_ui/phone_ui.cc"
    "phone_ui/phone_theme.cc"
    "phone_ui/phone_screen.cc"
    "phone_ui/phone_components.cc"
    "phone_ui/rodakos_layout.cc"
    "phone_ui/rodakos_theme.cc"
    "phone_ui/soft_keyboard.cc"
    "apps/built_in_apps.cc"
    "apps/home/home_app.cc"
    "apps/settings/settings_app.cc"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "." "rodakos_adapters" "phone_os" "phone_ui" "apps"
)
