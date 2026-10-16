#include "phone_ui/theme_presets.h"

#include "phone_os/appearance_service.h"
#include "phone_ui/phone_ui.h"

int ThemePresetIndex(const std::string& id) {
    for (size_t i = 0; i < kThemePresetOptions.size(); ++i) {
        if (id == kThemePresetOptions[i].id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool SelectLocalThemePreset(rodakos::AppearanceService* appearance,
                            PhoneUi* ui,
                            const ThemePresetOption& option) {
    if (appearance == nullptr ||
        !appearance->SetLocalTheme(option.id, rodakos_theme_preset_primary(option.preset))) {
        return false;
    }
    rodakos_theme_init_from_name(option.id);
    if (ui != nullptr) {
        ui->SyncThemeName(option.id);
    }
    return true;
}
