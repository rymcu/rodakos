#pragma once

namespace rodakos_test {
template <typename... Args>
inline void IgnoreLog(const char*, const char*, const Args&...) {}
}  // namespace rodakos_test

#define ESP_LOGE(...) ::rodakos_test::IgnoreLog(__VA_ARGS__)
#define ESP_LOGW(...) ::rodakos_test::IgnoreLog(__VA_ARGS__)
#define ESP_LOGI(...) ::rodakos_test::IgnoreLog(__VA_ARGS__)
#define ESP_LOGD(...) ::rodakos_test::IgnoreLog(__VA_ARGS__)
