#pragma once

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
inline const char* esp_err_to_name(esp_err_t) { return "host error"; }
