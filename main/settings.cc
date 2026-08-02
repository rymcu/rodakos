#include "settings.h"

#include <esp_log.h>

#include <limits>
#include <utility>

namespace {
constexpr const char* TAG = "Settings";
constexpr size_t kNvsNameMaxLength = 15;

bool IsValidNvsName(const std::string& name, const char* type) {
    if (name.size() <= kNvsNameMaxLength) {
        return true;
    }
    ESP_LOGE(TAG, "NVS %s '%s' too long", type, name.c_str());
    return false;
}
}  // namespace

Settings::Settings(const std::string& ns, bool read_write)
    : ns_(ns), read_write_(read_write) {
    if (!IsValidNvsName(ns_, "namespace")) {
        open_error_ = ESP_ERR_INVALID_ARG;
        return;
    }
    const esp_err_t err = nvs_open(ns_.c_str(), read_write ? NVS_READWRITE : NVS_READONLY, &handle_);
    open_error_ = err;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Open NVS namespace %s failed: %s", ns_.c_str(), esp_err_to_name(err));
        handle_ = 0;
    }
}

Settings::~Settings() {
    if (handle_ == 0) {
        return;
    }
    if (read_write_ && dirty_ && !commit_attempted_) {
        Commit();
    }
    nvs_close(handle_);
}

bool Settings::Commit() {
    if (!read_write_ || handle_ == 0) {
        return false;
    }
    if (!dirty_) {
        return true;
    }

    commit_attempted_ = true;
    const esp_err_t err = nvs_commit(handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Commit NVS namespace %s failed: %s", ns_.c_str(), esp_err_to_name(err));
        return false;
    }
    dirty_ = false;
    return true;
}

std::string Settings::GetString(const std::string& key, const std::string& default_value) {
    std::string value;
    if (ReadString(key, value, std::numeric_limits<size_t>::max()) !=
        SettingsStringReadStatus::kOk) {
        return default_value;
    }
    return value;
}

SettingsStringReadStatus Settings::ReadString(const std::string& key,
                                              std::string& value,
                                              size_t max_value_bytes) {
    value.clear();
    if (handle_ == 0) {
        return open_error_ == ESP_ERR_NVS_NOT_FOUND ? SettingsStringReadStatus::kNotFound
                                                    : SettingsStringReadStatus::kError;
    }
    if (!IsValidNvsName(key, "key")) {
        return SettingsStringReadStatus::kError;
    }

    size_t length = 0;
    const esp_err_t size_error = nvs_get_str(handle_, key.c_str(), nullptr, &length);
    if (size_error == ESP_ERR_NVS_NOT_FOUND) {
        return SettingsStringReadStatus::kNotFound;
    }
    if (size_error == ESP_ERR_NVS_TYPE_MISMATCH) {
        return SettingsStringReadStatus::kTypeMismatch;
    }
    if (size_error != ESP_OK || length == 0) {
        return SettingsStringReadStatus::kError;
    }
    if (length - 1 > max_value_bytes) {
        return SettingsStringReadStatus::kTooLarge;
    }

    std::string loaded(length, '\0');
    if (nvs_get_str(handle_, key.c_str(), loaded.data(), &length) != ESP_OK) {
        return SettingsStringReadStatus::kError;
    }
    while (!loaded.empty() && loaded.back() == '\0') {
        loaded.pop_back();
    }
    if (loaded.size() > max_value_bytes) {
        return SettingsStringReadStatus::kTooLarge;
    }
    value = std::move(loaded);
    return SettingsStringReadStatus::kOk;
}

bool Settings::SetString(const std::string& key, const std::string& value) {
    return WriteString(key, value) == SettingsStringWriteStatus::kOk;
}

SettingsStringWriteStatus Settings::WriteString(const std::string& key,
                                                const std::string& value) {
    if (!read_write_ || handle_ == 0 || !IsValidNvsName(key, "key")) {
        return SettingsStringWriteStatus::kError;
    }
    const esp_err_t err = nvs_set_str(handle_, key.c_str(), value.c_str());
    if (err == ESP_OK) {
        dirty_ = true;
        commit_attempted_ = false;
        return SettingsStringWriteStatus::kOk;
    }
    return err == ESP_ERR_NVS_REMOVE_FAILED ? SettingsStringWriteStatus::kRemoveFailed
                                            : SettingsStringWriteStatus::kError;
}

int32_t Settings::GetInt(const std::string& key, int32_t default_value) {
    if (handle_ == 0 || !IsValidNvsName(key, "key")) {
        return default_value;
    }
    int32_t value = default_value;
    if (nvs_get_i32(handle_, key.c_str(), &value) != ESP_OK) {
        return default_value;
    }
    return value;
}

bool Settings::SetInt(const std::string& key, int32_t value) {
    if (!read_write_ || handle_ == 0 || !IsValidNvsName(key, "key")) {
        return false;
    }
    if (nvs_set_i32(handle_, key.c_str(), value) == ESP_OK) {
        dirty_ = true;
        commit_attempted_ = false;
        return true;
    }
    return false;
}

bool Settings::GetBool(const std::string& key, bool default_value) {
    bool value = default_value;
    return ReadBool(key, value) == SettingsBoolReadStatus::kOk ? value : default_value;
}

SettingsBoolReadStatus Settings::ReadBool(const std::string& key, bool& value) {
    if (handle_ == 0) {
        return open_error_ == ESP_ERR_NVS_NOT_FOUND ? SettingsBoolReadStatus::kNotFound
                                                    : SettingsBoolReadStatus::kError;
    }
    if (!IsValidNvsName(key, "key")) {
        return SettingsBoolReadStatus::kError;
    }

    uint8_t loaded = 0;
    const esp_err_t err = nvs_get_u8(handle_, key.c_str(), &loaded);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return SettingsBoolReadStatus::kNotFound;
    }
    if (err == ESP_ERR_NVS_TYPE_MISMATCH) {
        return SettingsBoolReadStatus::kTypeMismatch;
    }
    if (err != ESP_OK) {
        return SettingsBoolReadStatus::kError;
    }
    value = loaded != 0;
    return SettingsBoolReadStatus::kOk;
}

bool Settings::SetBool(const std::string& key, bool value) {
    if (!read_write_ || handle_ == 0 || !IsValidNvsName(key, "key")) {
        return false;
    }
    if (nvs_set_u8(handle_, key.c_str(), value ? 1 : 0) == ESP_OK) {
        dirty_ = true;
        commit_attempted_ = false;
        return true;
    }
    return false;
}
