#pragma once

#include "phone_ui/rodakos_theme.h"

#include <array>
#include <cstdint>
#include <string>

class PhoneUi;

namespace rodakos {
class AppearanceService;
}

struct ThemePresetOption {
    const char* id;
    const char* label;
    const char* button_label;
    rodakos_theme_preset_t preset;
    uint32_t swatch;
    uint32_t label_color;
};

inline constexpr std::array<ThemePresetOption, 4> kThemePresetOptions = {{
    {"light", "浅色", "L", RODAKOS_THEME_LIGHT, 0xF7F7F7, 0x111111},
    {"dark", "深色", "D", RODAKOS_THEME_DARK, 0x111111, 0xFFFFFF},
    {"blue", "蓝色", "B", RODAKOS_THEME_BLUE, 0x1976D2, 0xFFFFFF},
    {"green", "绿色", "G", RODAKOS_THEME_GREEN, 0x388E3C, 0xFFFFFF},
}};

// Returns -1 for an unknown id.
int ThemePresetIndex(const std::string& id);

// Persists the preset as the local theme override, then applies it to the running UI.
// The caller still refreshes the current app (PhoneNavigation::RefreshTheme). Returns false
// without touching the runtime theme when the override cannot be saved.
bool SelectLocalThemePreset(rodakos::AppearanceService* appearance,
                            PhoneUi* ui,
                            const ThemePresetOption& option);
