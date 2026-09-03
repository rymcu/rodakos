#include "phone_os/voice_wake_settings.h"

#include "settings.h"

namespace rodakos {
namespace {
constexpr const char* kSettingsNamespace = "voice_wake";
constexpr const char* kEnabledKey = "enabled";
}

bool LoadVoiceWakeSettings(bool& enabled) {
    Settings settings(kSettingsNamespace, true);
    bool stored_enabled = false;
    const SettingsBoolReadStatus status = settings.ReadBool(kEnabledKey, stored_enabled);
    if (status == SettingsBoolReadStatus::kOk) {
        enabled = stored_enabled;
        return true;
    }
    if (status != SettingsBoolReadStatus::kNotFound) {
        return false;
    }
    if (!settings.SetBool(kEnabledKey, true) || !settings.Commit()) {
        return false;
    }
    enabled = true;
    return true;
}

bool SaveVoiceWakeSettings(bool enabled) {
    Settings settings(kSettingsNamespace, true);
    return settings.SetBool(kEnabledKey, enabled) && settings.Commit();
}

}  // namespace rodakos
