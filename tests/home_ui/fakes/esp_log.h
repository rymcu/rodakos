#pragma once

namespace rodakos_home_ui_test {

template <typename... Args>
inline void IgnoreLog(const char*, const char*, Args&&...) {}

}  // namespace rodakos_home_ui_test

#define RODAKOS_IGNORE_LOG(tag, format, ...) \
    ::rodakos_home_ui_test::IgnoreLog(tag, format __VA_OPT__(,) __VA_ARGS__)

#define ESP_LOGE(tag, format, ...) RODAKOS_IGNORE_LOG(tag, format, __VA_ARGS__)
#define ESP_LOGW(tag, format, ...) RODAKOS_IGNORE_LOG(tag, format, __VA_ARGS__)
#define ESP_LOGI(tag, format, ...) RODAKOS_IGNORE_LOG(tag, format, __VA_ARGS__)
#define ESP_LOGD(tag, format, ...) RODAKOS_IGNORE_LOG(tag, format, __VA_ARGS__)
