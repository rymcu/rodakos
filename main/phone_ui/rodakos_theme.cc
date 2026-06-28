#include "phone_ui/rodakos_theme.h"
#include <esp_log.h>

static const char* TAG = "Theme";

// 深色主题（默认）
static const rodakos_theme_t theme_dark = {
    // 基础色
    .primary = 0x79CBFF,      // 浅蓝色
    .secondary = 0xF2C76E,    // 金黄色
    .accent = 0xF27C7C,       // 珊瑚红

    // 背景色
    .bg_primary = 0x000000,   // 纯黑
    .bg_secondary = 0x1A1A1A, // 深灰
    .bg_tertiary = 0x2D2D2D,  // 中灰

    // 文字色
    .text_primary = 0xEAF7FF,   // 几乎白色
    .text_secondary = 0xBED4E1, // 浅灰蓝
    .text_tertiary = 0x8FB6C8,  // 中灰蓝

    // 状态色
    .success = 0x6FCF97,      // 绿色
    .warning = 0xF2C76E,      // 橙色
    .error = 0xF27C7C,        // 红色
    .info = 0x79CBFF,         // 蓝色

    // 特殊色
    .border = 0x3D3D3D,       // 边框灰
    .divider = 0x262626,      // 分割线
    .overlay = 0x000000,      // 遮罩（配合透明度）

    // 图标色板（柔和的彩色）
    .icon_palette = {
        0x5E7DAA,  // mist blue
        0x6D8C68,  // sage
        0xA07869,  // clay
        0x7F6FA7,  // lavender
        0xB08A55,  // brass
        0x5F9A9B,  // teal
        0xA0667A,  // rose
        0x6F86A1,  // steel
    },
};

// 浅色主题
static const rodakos_theme_t theme_light = {
    .primary = 0x2196F3,
    .secondary = 0xFF9800,
    .accent = 0xE91E63,

    .bg_primary = 0xFFFFFF,
    .bg_secondary = 0xF5F5F5,
    .bg_tertiary = 0xEEEEEE,

    .text_primary = 0x212121,
    .text_secondary = 0x757575,
    .text_tertiary = 0x9E9E9E,

    .success = 0x4CAF50,
    .warning = 0xFF9800,
    .error = 0xF44336,
    .info = 0x2196F3,

    .border = 0xE0E0E0,
    .divider = 0xBDBDBD,
    .overlay = 0x000000,

    .icon_palette = {
        0x5E7DAA, 0x6D8C68, 0xA07869, 0x7F6FA7,
        0xB08A55, 0x5F9A9B, 0xA0667A, 0x6F86A1,
    },
};

// 蓝色主题
static const rodakos_theme_t theme_blue = {
    .primary = 0x1976D2,
    .secondary = 0x64B5F6,
    .accent = 0x00BCD4,

    .bg_primary = 0x0D1B2A,
    .bg_secondary = 0x1B263B,
    .bg_tertiary = 0x415A77,

    .text_primary = 0xE0FBFC,
    .text_secondary = 0xC0E8F0,
    .text_tertiary = 0x778DA9,

    .success = 0x4CAF50,
    .warning = 0xFF9800,
    .error = 0xF44336,
    .info = 0x64B5F6,

    .border = 0x2C3E50,
    .divider = 0x1E2A38,
    .overlay = 0x0D1B2A,

    .icon_palette = {
        0x5E7DAA, 0x6D8C68, 0xA07869, 0x7F6FA7,
        0xB08A55, 0x5F9A9B, 0xA0667A, 0x6F86A1,
    },
};

// 绿色主题
static const rodakos_theme_t theme_green = {
    .primary = 0x388E3C,
    .secondary = 0x81C784,
    .accent = 0xFFEB3B,

    .bg_primary = 0x1B2A1F,
    .bg_secondary = 0x263D2B,
    .bg_tertiary = 0x3E5C45,

    .text_primary = 0xF1F8E9,
    .text_secondary = 0xDCEDC8,
    .text_tertiary = 0xAED581,

    .success = 0x66BB6A,
    .warning = 0xFFA726,
    .error = 0xEF5350,
    .info = 0x42A5F5,

    .border = 0x2E5033,
    .divider = 0x1F3823,
    .overlay = 0x1B2A1F,

    .icon_palette = {
        0x5E7DAA, 0x6D8C68, 0xA07869, 0x7F6FA7,
        0xB08A55, 0x5F9A9B, 0xA0667A, 0x6F86A1,
    },
};

// 当前激活的主题
static const rodakos_theme_t* current_theme = &theme_dark;
static rodakos_theme_t custom_theme;

void rodakos_theme_init(rodakos_theme_preset_t preset) {
    switch (preset) {
        case RODAKOS_THEME_DARK:
            current_theme = &theme_dark;
            ESP_LOGI(TAG, "Theme: Dark");
            break;
        case RODAKOS_THEME_LIGHT:
            current_theme = &theme_light;
            ESP_LOGI(TAG, "Theme: Light");
            break;
        case RODAKOS_THEME_BLUE:
            current_theme = &theme_blue;
            ESP_LOGI(TAG, "Theme: Blue");
            break;
        case RODAKOS_THEME_GREEN:
            current_theme = &theme_green;
            ESP_LOGI(TAG, "Theme: Green");
            break;
        case RODAKOS_THEME_CUSTOM:
            current_theme = &custom_theme;
            ESP_LOGI(TAG, "Theme: Custom");
            break;
        default:
            current_theme = &theme_dark;
            ESP_LOGW(TAG, "Unknown theme preset, using dark");
            break;
    }
}

void rodakos_theme_set_custom(const rodakos_theme_t* theme) {
    if (theme != nullptr) {
        custom_theme = *theme;
        current_theme = &custom_theme;
        ESP_LOGI(TAG, "Custom theme applied");
    }
}

const rodakos_theme_t* rodakos_theme_get(void) {
    return current_theme;
}

// 便捷访问函数
lv_color_t rodakos_theme_primary(void) {
    return lv_color_hex(current_theme->primary);
}

lv_color_t rodakos_theme_secondary(void) {
    return lv_color_hex(current_theme->secondary);
}

lv_color_t rodakos_theme_accent(void) {
    return lv_color_hex(current_theme->accent);
}

lv_color_t rodakos_theme_bg_primary(void) {
    return lv_color_hex(current_theme->bg_primary);
}

lv_color_t rodakos_theme_bg_secondary(void) {
    return lv_color_hex(current_theme->bg_secondary);
}

lv_color_t rodakos_theme_bg_tertiary(void) {
    return lv_color_hex(current_theme->bg_tertiary);
}

lv_color_t rodakos_theme_text_primary(void) {
    return lv_color_hex(current_theme->text_primary);
}

lv_color_t rodakos_theme_text_secondary(void) {
    return lv_color_hex(current_theme->text_secondary);
}

lv_color_t rodakos_theme_text_tertiary(void) {
    return lv_color_hex(current_theme->text_tertiary);
}

lv_color_t rodakos_theme_success(void) {
    return lv_color_hex(current_theme->success);
}

lv_color_t rodakos_theme_warning(void) {
    return lv_color_hex(current_theme->warning);
}

lv_color_t rodakos_theme_error(void) {
    return lv_color_hex(current_theme->error);
}

lv_color_t rodakos_theme_info(void) {
    return lv_color_hex(current_theme->info);
}

lv_color_t rodakos_theme_border(void) {
    return lv_color_hex(current_theme->border);
}

lv_color_t rodakos_theme_divider(void) {
    return lv_color_hex(current_theme->divider);
}

lv_color_t rodakos_theme_overlay(void) {
    return lv_color_hex(current_theme->overlay);
}

lv_color_t rodakos_theme_icon_color(int index) {
    if (index < 0 || index >= 8) {
        index = 0;
    }
    return lv_color_hex(current_theme->icon_palette[index % 8]);
}

void rodakos_theme_apply_to_lvgl(void) {
    // TODO: 如果需要，可以设置 LVGL 全局主题
    ESP_LOGI(TAG, "LVGL theme integration (optional)");
}
