#pragma once

using esp_log_level_t = int;
constexpr esp_log_level_t ESP_LOG_NONE = 0;
inline esp_log_level_t esp_log_level_get(const char*) { return ESP_LOG_NONE; }
inline void esp_log_level_set(const char*, esp_log_level_t) {}
