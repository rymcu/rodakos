#pragma once

#include <atomic>
#include <cstring>
#include <iterator>
#include <esp_log.h>

namespace rodakos {

// Values after kMqttConfig are TLS/NVS failures, not allocations; they reuse the same one-shot slot.
enum class ResourceFailure {
    kNone, kHomePage, kImage, kCameraTask, kVoiceTask, kMqttConfig,
    kOtaDownloadRead, kCloudTls, kOtaJournalSave, kCloudPersist,
};

#ifdef RODAKOS_RELEASE_TESTS
inline std::atomic<ResourceFailure> g_resource_failure{ResourceFailure::kNone};
inline bool ArmResourceFailure(const char* name) {
    constexpr const char* names[] = {"clear", "home_page", "image", "camera_task", "voice_task",
                                     "mqtt_config", "ota_download_read", "cloud_tls",
                                     "ota_journal_save", "cloud_persist"};
    for (int index = 0; index < static_cast<int>(std::size(names)); ++index) {
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
