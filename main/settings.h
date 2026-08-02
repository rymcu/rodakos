#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <nvs_flash.h>

enum class SettingsStringReadStatus {
    kOk,
    kNotFound,
    kTypeMismatch,
    kTooLarge,
    kError,
};

enum class SettingsStringWriteStatus {
    kOk,
    kRemoveFailed,
    kError,
};

enum class SettingsBoolReadStatus {
    kOk,
    kNotFound,
    kTypeMismatch,
    kError,
};

class Settings {
public:
    explicit Settings(const std::string& ns, bool read_write = false);
    ~Settings();

    std::string GetString(const std::string& key, const std::string& default_value = "");
    SettingsStringReadStatus ReadString(const std::string& key,
                                        std::string& value,
                                        size_t max_value_bytes);
    bool SetString(const std::string& key, const std::string& value);
    SettingsStringWriteStatus WriteString(const std::string& key, const std::string& value);
    int32_t GetInt(const std::string& key, int32_t default_value = 0);
    bool SetInt(const std::string& key, int32_t value);
    bool GetBool(const std::string& key, bool default_value = false);
    SettingsBoolReadStatus ReadBool(const std::string& key, bool& value);
    bool SetBool(const std::string& key, bool value);
    bool Commit();

private:
    std::string ns_;
    nvs_handle_t handle_ = 0;
    esp_err_t open_error_ = ESP_OK;
    bool read_write_ = false;
    bool dirty_ = false;
    bool commit_attempted_ = false;
};
