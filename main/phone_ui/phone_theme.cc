#include "phone_ui/phone_theme.h"
#include "phone_ui/rodakos_theme.h"
#include <cmath>

PhoneTheme PhoneThemeFromRodakos() {
    const auto* theme = rodakos_theme_get();
    const auto channel = [](uint32_t value) {
        const float normalized = static_cast<float>(value) / 255.0F;
        return normalized <= 0.04045F ? normalized / 12.92F
            : std::pow((normalized + 0.055F) / 1.055F, 2.4F);
    };
    const float luminance = 0.2126F * channel((theme->primary >> 16) & 255) +
                            0.7152F * channel((theme->primary >> 8) & 255) +
                            0.0722F * channel(theme->primary & 255);
    return {
        .background = lv_color_hex(theme->bg_primary),
        .surface = lv_color_hex(theme->bg_secondary),
        .surface_alt = lv_color_hex(theme->bg_tertiary),
        .border = lv_color_hex(theme->border),
        .text_primary = lv_color_hex(theme->text_primary),
        .text_secondary = lv_color_hex(theme->text_secondary),
        .accent = lv_color_hex(theme->primary),
        .accent_text = lv_color_hex(luminance > 0.179F ? 0x000000 : 0xffffff),
    };
}

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
