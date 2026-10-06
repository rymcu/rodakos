#include "phone_os/voice_wake_settings.h"

#include "settings.h"

#include <array>
#include <charconv>
#include <limits>
#include <utility>

namespace rodakos {
namespace {
constexpr const char* kSettingsNamespace = "voice_wake";
constexpr const char* kEnabledKey = "enabled";
constexpr const char* kIdentityKey = "identity";

template <typename T>
bool ParseInteger(const std::string& value, T& result) {
    if (value.empty()) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

SettingsStringReadStatus ReadRecord(std::string& json) {
    Settings settings(kSettingsNamespace, false);
    return settings.ReadString(kIdentityKey, json, kVoiceIdentityRecordMaxBytes);
}

enum class LegacyStatus { kMissing, kLoaded, kError };

LegacyStatus LoadLegacyConfig(Settings& settings, const char* prefix, VoiceIdentityConfig& config) {
    constexpr std::array<const char*, 6> suffixes{"_name", "_word", "_cmd", "_rev", "_mode", "_exp"};
    std::array<std::string, 6> values;
    size_t missing = 0;
    for (size_t i = 0; i < suffixes.size(); ++i) {
        const auto status = settings.ReadString(std::string(prefix) + suffixes[i], values[i], 128);
        if (status == SettingsStringReadStatus::kNotFound) ++missing;
        else if (status != SettingsStringReadStatus::kOk) return LegacyStatus::kError;
    }
    if (missing == suffixes.size()) return LegacyStatus::kMissing;
    if (missing != 0) return LegacyStatus::kError;
    config.name = values[0];
    config.wake_word = values[1];
    config.wake_command = values[2];
    if (!ParseInteger(values[3], config.revision) || config.revision == 0 ||
        !ParseInteger(values[5], config.expires_at_ms)) return LegacyStatus::kError;
    if (values[4] == "0") config.mode = VoiceIdentityApplyMode::kPersistent;
    else if (values[4] == "1") config.mode = VoiceIdentityApplyMode::kTemporary;
    else return LegacyStatus::kError;
    return LegacyStatus::kLoaded;
}
}  // namespace

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

bool LoadVoiceWakeIdentitySettings(VoiceIdentityRecord& record, std::string& error) {
    std::string json;
    const auto status = ReadRecord(json);
    if (status == SettingsStringReadStatus::kOk) return DecodeVoiceIdentityRecord(json, record, error);
    if (status != SettingsStringReadStatus::kNotFound) {
        error = "failed to read voice identity record";
        return false;
    }

    VoiceIdentityRecord migrated;
    Settings legacy(kSettingsNamespace, false);
    const auto persistent = LoadLegacyConfig(legacy, "p", migrated.persistent);
    const auto active = LoadLegacyConfig(legacy, "a", migrated.active);
    if (persistent == LegacyStatus::kError || active == LegacyStatus::kError || persistent != active) {
        error = "incomplete or invalid legacy voice identity";
        return false;
    }
    migrated.last_accepted = migrated.active;
    // Validate the whole legacy snapshot before creating the sole authoritative key.
    if (!EncodeVoiceIdentityRecord(migrated, json, error) ||
        SaveVoiceWakeIdentitySettings(migrated, error) != VoiceIdentitySaveStatus::kSaved) return false;
    record = std::move(migrated);
    return true;
}

VoiceIdentitySaveStatus SaveVoiceWakeIdentitySettings(const VoiceIdentityRecord& record,
                                                     std::string& error) {
    std::string encoded;
    if (!EncodeVoiceIdentityRecord(record, encoded, error)) return VoiceIdentitySaveStatus::kUnchanged;
    std::string previous;
    const auto before = ReadRecord(previous);
    if (before != SettingsStringReadStatus::kNotFound && before != SettingsStringReadStatus::kOk) {
        error = "voice identity storage outcome is uncertain";
        return VoiceIdentitySaveStatus::kIndeterminate;
    }
    if (before == SettingsStringReadStatus::kOk) {
        VoiceIdentityRecord verified;
        if (!DecodeVoiceIdentityRecord(previous, verified, error))
            return VoiceIdentitySaveStatus::kIndeterminate;
    }
    SettingsStringWriteStatus write_status;
    bool committed = false;
    {
        Settings writer(kSettingsNamespace, true);
        write_status = writer.WriteString(kIdentityKey, encoded);
        // Explicit Commit also prevents destructor-only completion after a reported failure.
        committed = writer.Commit();
    }
    std::string current;
    const auto after = ReadRecord(current);
    if (write_status == SettingsStringWriteStatus::kOk && committed &&
        after == SettingsStringReadStatus::kOk && current == encoded) {
        error.clear();
        return VoiceIdentitySaveStatus::kSaved;
    }
    if (write_status != SettingsStringWriteStatus::kRemoveFailed &&
        after == before && current == previous) {
        error = "failed to persist voice identity";
        return VoiceIdentitySaveStatus::kUnchanged;
    }
    // NVS may return an error after writing the new item; an error is not rollback evidence.
    error = "voice identity storage outcome is uncertain";
    return VoiceIdentitySaveStatus::kIndeterminate;
}

}  // namespace rodakos
