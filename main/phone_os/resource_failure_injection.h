#pragma once

#include <atomic>
#include <cstring>
#include <esp_log.h>

namespace rodakos {

enum class ResourceFailure { kNone, kHomePage, kImage, kCameraTask, kVoiceTask, kMqttConfig };

#ifdef RODAKOS_RELEASE_TESTS
inline std::atomic<ResourceFailure> g_resource_failure{ResourceFailure::kNone};
inline bool ArmResourceFailure(const char* name) {
    constexpr const char* names[] = {"clear", "home_page", "image", "camera_task", "voice_task", "mqtt_config"};
    for (int index = 0; index < 6; ++index) {
        if (std::strcmp(name, names[index]) == 0) {
            g_resource_failure.store(static_cast<ResourceFailure>(index));
            return true;
        }
    }
    return false;
}
inline bool FailResource(ResourceFailure point) {
    auto expected = point;
    if (!g_resource_failure.compare_exchange_strong(expected, ResourceFailure::kNone)) {
        return false;
    }
    ESP_LOGW("ReleaseFault", "RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE allocation=%d", static_cast<int>(point));
    return true;
}
#else
inline bool FailResource(ResourceFailure) { return false; }
#endif

}  // namespace rodakos
