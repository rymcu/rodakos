#include "phone_ui/phone_theme.h"

PhoneTheme PhoneDarkTheme() {
    return {
        .background = lv_color_hex(0x080D12),
        .surface = lv_color_hex(0x132330),
        .surface_alt = lv_color_hex(0x1E3545),
        .border = lv_color_hex(0x2D5268),
        .text_primary = lv_color_hex(0xF4FAFF),
        .text_secondary = lv_color_hex(0x9AB6C7),
        .accent = lv_color_hex(0x6EC6FF),
        .accent_text = lv_color_hex(0x06131D),
    };
}

PhoneTheme PhoneLightTheme() {
    return {
        .background = lv_color_hex(0xEFF5F7),
        .surface = lv_color_hex(0xFFFFFF),
        .surface_alt = lv_color_hex(0xDDEBF1),
        .border = lv_color_hex(0xB4CAD5),
        .text_primary = lv_color_hex(0x13202A),
        .text_secondary = lv_color_hex(0x5D7280),
        .accent = lv_color_hex(0x1877A8),
        .accent_text = lv_color_hex(0xFFFFFF),
    };
}
