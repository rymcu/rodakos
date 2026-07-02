#pragma once

#include "phone_ui/rodakos_theme.h"

#include <lvgl.h>
#include <array>
#include <cstdint>
#include <string>

class PhoneUi;

namespace rodakos_settings {

inline constexpr const char* kDisplayNamespace = "display";
inline constexpr const char* kBrightnessKey = "brightness";
inline constexpr const char* kThemeKey = "theme";
inline constexpr const char* kLanguageKey = "language";
inline constexpr uint32_t kTimeSyncTimeoutPolls = 20;

struct ThemeOption {
    const char* id;
    const char* label;
    const char* button_label;
    rodakos_theme_preset_t preset;
    bool phone_ui_light;
    uint32_t swatch;
    uint32_t label_color;
};

inline constexpr std::array<ThemeOption, 4> kThemeOptions = {{
    {"light", "Light", "L", RODAKOS_THEME_LIGHT, true, 0xF7F7F7, 0x111111},
    {"dark", "Dark", "D", RODAKOS_THEME_DARK, false, 0x111111, 0xFFFFFF},
    {"blue", "Blue", "B", RODAKOS_THEME_BLUE, false, 0x1976D2, 0xFFFFFF},
    {"green", "Green", "G", RODAKOS_THEME_GREEN, false, 0x388E3C, 0xFFFFFF},
}};

void DeferReturnHome(void* user_data);
void DeferReloadSettings(void* user_data);
std::string TrimServerName(const char* text);
std::string TrimCloudUrl(const char* text);
void UpdateBrightnessLabel(lv_obj_t* label, int value);
int ThemeIndexFromId(const std::string& theme);
void ApplyThemeToRuntime(PhoneUi* ui, const ThemeOption& option);
lv_obj_t* CreateSettingCard(lv_obj_t* parent, lv_coord_t y_offset, lv_coord_t height = 50);
lv_obj_t* CreateSettingLabel(lv_obj_t* parent, const char* text, bool secondary = false);
lv_obj_t* CreateSettingIcon(lv_obj_t* parent, const char* icon);

}  // namespace rodakos_settings
