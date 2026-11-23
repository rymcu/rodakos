#pragma once

#include "phone_os/light_service.h"
#include "phone_os/voice_assistant_state.h"
#include "phone_os/voice_status_light_model.h"

#include <cstdint>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace rodakos {

struct VoiceStatusLightSettings {
    bool enabled = true;
    // Maximum brightness of the status light, 1..100. The default keeps a bare LED bearable
    // at night.
    uint8_t max_brightness_percent = 30;
};

bool LoadVoiceStatusLightSettings(VoiceStatusLightSettings& settings);
bool SaveVoiceStatusLightSettings(const VoiceStatusLightSettings& settings);

// Shows the voice assistant state on the first board light (docs/voice-assistant.md, Status
// Light). It only reads assistant snapshots and writes through the LightService overlay, so the
// user light configuration and the MQTT report never change.
class VoiceStatusLightService {
public:
    using SnapshotSource = std::function<VoiceStatusLightSnapshot()>;

    VoiceStatusLightService(LightService& lights, SnapshotSource source);

    // Starts the permanent animation task once. Later calls return the first result.
    bool Start();

    VoiceStatusLightSettings settings();
    // Saves and applies at once. Turning the light off removes a running overlay.
    bool SetSettings(const VoiceStatusLightSettings& settings);

    // One animation step; the task calls it every frame. Public for host tests.
    void Tick(int64_t now_ms);

private:
    static void TaskEntry(void* arg);

    LightService& lights_;
    SnapshotSource source_;
    SemaphoreHandle_t mutex_ = nullptr;
    VoiceStatusLightSettings settings_;
    VoiceStatusLightModel model_;
    bool started_ = false;
    bool start_ok_ = false;
    bool overlay_shown_ = false;
    RgbColor last_color_{};
};

}  // namespace rodakos
