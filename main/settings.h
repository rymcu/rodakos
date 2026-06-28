#pragma once

#include <cstdint>
#include <string>

#include <nvs_flash.h>

class Settings {
public:
    explicit Settings(const std::string& ns, bool read_write = false);
    ~Settings();

    std::string GetString(const std::string& key, const std::string& default_value = "");
    void SetString(const std::string& key, const std::string& value);
    int32_t GetInt(const std::string& key, int32_t default_value = 0);
    void SetInt(const std::string& key, int32_t value);
    bool GetBool(const std::string& key, bool default_value = false);
    void SetBool(const std::string& key, bool value);

private:
    std::string ns_;
    nvs_handle_t handle_ = 0;
    bool read_write_ = false;
    bool dirty_ = false;
};
