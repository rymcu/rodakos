#include "apps/settings/settings_app_internal.h"

#include "apps/settings/settings_app.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_navigation.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

namespace rodakos_settings {

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        lv_indev_reset(nullptr, nullptr);
        context->navigation().ReturnHome();
    }
}

void DeferReloadSettings(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        lv_indev_reset(nullptr, nullptr);
        context->navigation().RefreshTheme();
    }
}

std::string TrimServerName(const char* text) {
    if (text == nullptr) {
        return "";
    }
    std::string value(text);
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
    if (value.size() > 63) {
        value.resize(63);
    }
    return value;
}

std::string TrimCloudUrl(const char* text) {
    if (text == nullptr) {
        return "";
    }
    std::string value(text);
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
    if (value.size() > 191) {
        value.resize(191);
    }
    return value;
}

void UpdateBrightnessLabel(lv_obj_t* label, int value) {
    if (label != nullptr) {
        lv_label_set_text_fmt(label, "%d%%", value);
    }
}

int ThemeIndexFromId(const std::string& theme) {
    for (size_t i = 0; i < kThemeOptions.size(); ++i) {
        if (theme == kThemeOptions[i].id) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void ApplyThemeToRuntime(PhoneUi* ui, const ThemeOption& option) {
    rodakos_theme_init_from_name(option.id);
    if (ui != nullptr) {
        ui->SetThemeName(option.phone_ui_light ? "light" : "dark");
    }
}

lv_obj_t* CreateSettingCard(lv_obj_t* parent, lv_coord_t y_offset, lv_coord_t height) {
    auto* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 300, height);
    lv_obj_set_pos(card, (320 - 300) / 2, y_offset);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t* CreateSettingLabel(lv_obj_t* parent, const char* text, bool secondary) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label,
        secondary ? rodakos_theme_text_secondary() : rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    return label;
}

lv_obj_t* CreateSettingIcon(lv_obj_t* parent, const char* icon) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_color(label, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(label, PhoneIconFont(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return label;
}

}  // namespace rodakos_settings
