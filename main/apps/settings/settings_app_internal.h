#pragma once

#include "phone_ui/rodakos_theme.h"

#include <lvgl.h>
#include <cstdint>
#include <string>

namespace rodakos_settings {

inline constexpr const char* kDisplayNamespace = "display";
inline constexpr const char* kBrightnessKey = "brightness";
inline constexpr const char* kLanguageKey = "language";
inline constexpr uint32_t kTimeSyncTimeoutPolls = 20;

void DeferReturnHome(void* user_data);
void DeferOpenThemes(void* user_data);
std::string TrimServerName(const char* text);
std::string TrimCloudUrl(const char* text);
void UpdateBrightnessLabel(lv_obj_t* label, int value);
lv_obj_t* CreateSettingCard(lv_obj_t* parent, lv_coord_t y_offset, lv_coord_t height = 50);
lv_obj_t* CreateSettingLabel(lv_obj_t* parent, const char* text, bool secondary = false);
lv_obj_t* CreateSettingIcon(lv_obj_t* parent, const char* icon);

}  // namespace rodakos_settings
