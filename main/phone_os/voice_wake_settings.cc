#include "phone_os/voice_wake_settings.h"

#include "settings.h"

#include <cstdlib>

namespace rodakos {
namespace {
constexpr const char* kSettingsNamespace = "voice_wake";
constexpr const char* kEnabledKey = "enabled";

int64_t ParseInteger(const std::string& value, int64_t fallback) {
    if (value.empty()) return fallback;
    char* end = nullptr;
    const long long parsed = std::strtoll(value.c_str(), &end, 10);
    return end == value.c_str() || *end != '\0' ? fallback : parsed;
}

bool SaveIdentityFields(Settings& settings, const char* prefix, const VoiceIdentityConfig& config) {
    const std::string p(prefix);
    return settings.SetString(p + "_name", config.name) &&
           settings.SetString(p + "_word", config.wake_word) &&
           settings.SetString(p + "_cmd", config.wake_command) &&
           settings.SetString(p + "_rev", std::to_string(config.revision)) &&
           settings.SetString(p + "_mode", config.mode == VoiceIdentityApplyMode::kTemporary ? "1" : "0") &&
           settings.SetString(p + "_exp", std::to_string(config.expires_at_ms));
}

VoiceIdentityConfig LoadIdentityFields(Settings& settings,
                                       const char* prefix,
                                       const VoiceIdentityConfig& fallback) {
    const std::string p(prefix);
    VoiceIdentityConfig config = fallback;
    config.name = settings.GetString(p + "_name", fallback.name);
    config.wake_word = settings.GetString(p + "_word", fallback.wake_word);
    config.wake_command = settings.GetString(p + "_cmd", fallback.wake_command);
    config.revision = static_cast<uint32_t>(ParseInteger(
        settings.GetString(p + "_rev", std::to_string(fallback.revision)), fallback.revision));
    config.mode = ParseInteger(
                      settings.GetString(p + "_mode", fallback.mode == VoiceIdentityApplyMode::kTemporary ? "1" : "0"),
                      fallback.mode == VoiceIdentityApplyMode::kTemporary ? 1 : 0) == 1
                      ? VoiceIdentityApplyMode::kTemporary
                      : VoiceIdentityApplyMode::kPersistent;
    config.expires_at_ms = ParseInteger(settings.GetString(p + "_exp", "0"), 0);
    return config;
}
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

bool LoadVoiceWakeIdentitySettings(VoiceIdentityConfig& persistent,
                                   VoiceIdentityConfig& active) {
    Settings settings(kSettingsNamespace, true);
    const VoiceIdentityConfig defaults = DefaultVoiceIdentityConfig();
    const std::string stored_name = settings.GetString("p_name", "");
    if (stored_name.empty()) {
        persistent = defaults;
        active = defaults;
        return SaveVoiceWakeIdentitySettings(persistent, active);
    }

    persistent = LoadIdentityFields(settings, "p", defaults);
    active = LoadIdentityFields(settings, "a", persistent);
    return true;
}

bool SaveVoiceWakeIdentitySettings(const VoiceIdentityConfig& persistent,
                                   const VoiceIdentityConfig& active) {
    Settings settings(kSettingsNamespace, true);
    return SaveIdentityFields(settings, "p", persistent) &&
           SaveIdentityFields(settings, "a", active) && settings.Commit();
}

}  // namespace rodakos
