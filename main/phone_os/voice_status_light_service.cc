#include "phone_os/voice_status_light_service.h"

#include "settings.h"

#include <algorithm>

#include <esp_log.h>
#include <esp_timer.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceStatusLight";
constexpr const char* kSettingsNamespace = "voice_light";
constexpr const char* kEnabledKey = "enabled";
constexpr const char* kMaxBrightnessKey = "max_bright";
constexpr size_t kLightIndex = 0;
// The LED write goes through RMT and LightService; 3 KiB leaves room for its logging path.
constexpr uint32_t kTaskStackBytes = 3072;
constexpr UBaseType_t kTaskPriority = 2;
constexpr TickType_t kFramePeriod = pdMS_TO_TICKS(33);

uint8_t Scale(uint8_t channel, uint32_t percent) {
    return static_cast<uint8_t>((channel * percent + 50) / 100);
}
}  // namespace

bool LoadVoiceStatusLightSettings(VoiceStatusLightSettings& settings) {
    Settings store(kSettingsNamespace, false);
    settings.enabled = store.GetBool(kEnabledKey, true);
    const int32_t brightness = store.GetInt(kMaxBrightnessKey, 30);
    settings.max_brightness_percent = static_cast<uint8_t>(std::clamp<int32_t>(brightness, 1, 100));
    return true;
}

bool SaveVoiceStatusLightSettings(const VoiceStatusLightSettings& settings) {
    Settings store(kSettingsNamespace, true);
    return store.SetBool(kEnabledKey, settings.enabled) &&
           store.SetInt(kMaxBrightnessKey, settings.max_brightness_percent) && store.Commit();
}

VoiceStatusLightService::VoiceStatusLightService(LightService& lights, SnapshotSource source)
    : lights_(lights), source_(std::move(source)) {
    mutex_ = xSemaphoreCreateMutex();
}

bool VoiceStatusLightService::Start() {
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (started_) {
        const bool ok = start_ok_;
        xSemaphoreGive(mutex_);
        return ok;
    }
    started_ = true;
    LoadVoiceStatusLightSettings(settings_);
    xSemaphoreGive(mutex_);

    if (!lights_.IsAvailable()) {
        ESP_LOGW(TAG, "No board light; voice status light disabled");
        return false;
    }
    // Permanent task: it never exits, so it needs no retirement record. Its RMT write and
    // snapshot read never touch flash or NVS, so the stack can stay out of internal SRAM.
    const BaseType_t created = xTaskCreateWithCaps(TaskEntry, "voice_light", kTaskStackBytes, this,
                                                   kTaskPriority, nullptr,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    start_ok_ = created == pdPASS;
    xSemaphoreGive(mutex_);
    if (!start_ok_) {
        ESP_LOGE(TAG, "Failed to create the voice status light task");
    }
    return start_ok_;
}

VoiceStatusLightSettings VoiceStatusLightService::settings() {
    VoiceStatusLightSettings copy;
    if (mutex_ == nullptr) return copy;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    copy = settings_;
    xSemaphoreGive(mutex_);
    return copy;
}

bool VoiceStatusLightService::SetSettings(const VoiceStatusLightSettings& settings) {
    if (mutex_ == nullptr) return false;
    VoiceStatusLightSettings next = settings;
    next.max_brightness_percent = std::clamp<uint8_t>(next.max_brightness_percent, 1, 100);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    settings_ = next;
    xSemaphoreGive(mutex_);
    return SaveVoiceStatusLightSettings(next);
}

void VoiceStatusLightService::Tick(int64_t now_ms) {
    const VoiceStatusLightSnapshot snapshot = source_ ? source_() : VoiceStatusLightSnapshot{};
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const VoiceStatusLightSettings settings = settings_;
    const VoiceStatusLightFrame frame = model_.Update(snapshot, now_ms);
    xSemaphoreGive(mutex_);

    if (!settings.enabled || !frame.active) {
        if (overlay_shown_) {
            lights_.SetOverlay(kLightIndex, std::nullopt);
            overlay_shown_ = false;
        }
        return;
    }

    const uint32_t percent = frame.level * settings.max_brightness_percent / 100;
    const RgbColor color{Scale(frame.red, percent), Scale(frame.green, percent),
                         Scale(frame.blue, percent)};
    if (overlay_shown_ && color.red == last_color_.red && color.green == last_color_.green &&
        color.blue == last_color_.blue) {
        return;
    }
    if (lights_.SetOverlay(kLightIndex, color) == ESP_OK) {
        overlay_shown_ = true;
        last_color_ = color;
    }
}

void VoiceStatusLightService::TaskEntry(void* arg) {
    auto* self = static_cast<VoiceStatusLightService*>(arg);
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        self->Tick(esp_timer_get_time() / 1000);
        vTaskDelayUntil(&wake, kFramePeriod);
    }
}

}  // namespace rodakos
