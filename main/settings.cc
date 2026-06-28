#include "settings.h"

#include <esp_log.h>

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
        return;
    }
    const esp_err_t err = nvs_open(ns_.c_str(), read_write ? NVS_READWRITE : NVS_READONLY, &handle_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Open NVS namespace %s failed: %s", ns_.c_str(), esp_err_to_name(err));
        handle_ = 0;
    }
}

Settings::~Settings() {
    if (handle_ == 0) {
        return;
    }
    if (read_write_ && dirty_) {
        const esp_err_t err = nvs_commit(handle_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Commit NVS namespace %s failed: %s", ns_.c_str(), esp_err_to_name(err));
        }
    }
    nvs_close(handle_);
}

std::string Settings::GetString(const std::string& key, const std::string& default_value) {
    if (handle_ == 0 || !IsValidNvsName(key, "key")) {
        return default_value;
    }

    size_t length = 0;
    if (nvs_get_str(handle_, key.c_str(), nullptr, &length) != ESP_OK || length == 0) {
        return default_value;
    }

    std::string value(length, '\0');
    if (nvs_get_str(handle_, key.c_str(), value.data(), &length) != ESP_OK) {
        return default_value;
    }
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

void Settings::SetString(const std::string& key, const std::string& value) {
    if (!read_write_ || handle_ == 0 || !IsValidNvsName(key, "key")) {
        return;
    }
    if (nvs_set_str(handle_, key.c_str(), value.c_str()) == ESP_OK) {
        dirty_ = true;
    }
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

void Settings::SetInt(const std::string& key, int32_t value) {
    if (!read_write_ || handle_ == 0 || !IsValidNvsName(key, "key")) {
        return;
    }
    if (nvs_set_i32(handle_, key.c_str(), value) == ESP_OK) {
        dirty_ = true;
    }
}

bool Settings::GetBool(const std::string& key, bool default_value) {
    if (handle_ == 0 || !IsValidNvsName(key, "key")) {
        return default_value;
    }
    uint8_t value = default_value ? 1 : 0;
    if (nvs_get_u8(handle_, key.c_str(), &value) != ESP_OK) {
        return default_value;
    }
    return value != 0;
}

void Settings::SetBool(const std::string& key, bool value) {
    if (!read_write_ || handle_ == 0 || !IsValidNvsName(key, "key")) {
        return;
    }
    if (nvs_set_u8(handle_, key.c_str(), value ? 1 : 0) == ESP_OK) {
        dirty_ = true;
    }
}
