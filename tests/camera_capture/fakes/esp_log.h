#pragma once

#include "host_runtime.h"

namespace camera_host {
template <typename... Args>
inline void TestLog(const char*, const char* format, const Args&...) {
    ObserveLog(format);
}
}

#define ESP_LOGE(...) ::camera_host::TestLog(__VA_ARGS__)
#define ESP_LOGW(...) ::camera_host::TestLog(__VA_ARGS__)
#define ESP_LOGI(...) ::camera_host::TestLog(__VA_ARGS__)
#define ESP_LOGD(...) ::camera_host::TestLog(__VA_ARGS__)
