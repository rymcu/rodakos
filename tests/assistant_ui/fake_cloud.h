#pragma once
#include "phone_os/device_cloud_config.h"
#include <functional>

namespace cloud_ui_test {
inline rodakos::CloudDiagnosticState state;
inline rodakos::DeviceCloudConfig config;
inline unsigned refreshes = 0;
inline unsigned unbinds = 0;
inline bool refresh_ok = true;
inline std::function<void()> on_refresh;
inline void Reset() {
    state = {}; config = {}; refreshes = 0; unbinds = 0;
    refresh_ok = true; on_refresh = {};
}
}
