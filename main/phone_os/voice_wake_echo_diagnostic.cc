#include "phone_os/voice_wake_echo_diagnostic.h"

#include "phone_os/audio_output_service.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "WakeEchoDiagnostic";
constexpr const char* kOwner = "wake-echo-diagnostic";
constexpr uint32_t kSampleRate = 16000;
constexpr size_t kBlockSamples = 320;
constexpr uint32_t kMaxSeconds = 600;

struct EchoRequest {
    AudioOutputService* output = nullptr;
    uint32_t seconds = 0;
    float amplitude = 0.0F;
};

std::atomic<bool> running{false};
std::atomic<bool> stop_requested{false};
EchoRequest request;

// Band-limited noise (two one-pole sections, roughly 300 Hz–3 kHz) under a 4 Hz syllable
// envelope: speech-like echo energy without any intelligible content to wake on.
void EchoTask(void*) {
    const EchoRequest active = request;
    uint32_t seed = 0x13579BDFU;
    float low = 0.0F, high_state = 0.0F, previous = 0.0F;
    const float low_alpha = 1.0F - std::exp(-2.0F * 3.14159265F * 3000.0F / kSampleRate);
    const float high_alpha = std::exp(-2.0F * 3.14159265F * 300.0F / kSampleRate);
    std::vector<int16_t> block(kBlockSamples);
    const int64_t end_us = esp_timer_get_time() + static_cast<int64_t>(active.seconds) * 1000000;
    uint64_t sample_index = 0;
    bool ok = true;
    ESP_LOGI(TAG, "Echo playback started: seconds=%u amplitude=%.3f", static_cast<unsigned>(active.seconds),
             static_cast<double>(active.amplitude));
    while (ok && !stop_requested.load() && esp_timer_get_time() < end_us) {
        for (auto& out : block) {
            seed = seed * 1664525U + 1013904223U;
            const float white = static_cast<float>(static_cast<int32_t>(seed >> 8) - (1 << 23)) / (1 << 23);
            low += low_alpha * (white - low);
            high_state = high_alpha * (high_state + low - previous);
            previous = low;
            const float t = static_cast<float>(sample_index++) / kSampleRate;
            const float envelope = 0.55F + 0.45F * std::sin(2.0F * 3.14159265F * 4.0F * t);
            const float value = std::max(-1.0F, std::min(1.0F, 4.0F * high_state * envelope));
            out = static_cast<int16_t>(value * active.amplitude * 32767.0F);
        }
        // A write fails once another owner (an assistant session or music) takes the speaker.
        ok = active.output->WriteForOwner(kOwner, block.data(),
                                          static_cast<int>(block.size() * sizeof(int16_t)));
    }
    active.output->CloseForOwner(kOwner);
    ESP_LOGI(TAG, "Echo playback finished: written_ms=%u reason=%s",
             static_cast<unsigned>(sample_index * 1000 / kSampleRate),
             !ok ? "output_lost" : stop_requested.load() ? "stopped" : "elapsed");
    running.store(false);
    vTaskDelete(nullptr);
}

bool ParseUnsigned(const std::string& text, uint32_t limit, uint32_t& value) {
    if (text.empty() || text.size() > 4) return false;
    value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    return value <= limit;
}
}  // namespace

bool StartWakeEchoDiagnostic(AudioOutputService& output, const std::string& arguments) {
    const size_t space = arguments.find(' ');
    uint32_t seconds = 0;
    uint32_t attenuation_db = 0;
    if (space == std::string::npos || !ParseUnsigned(arguments.substr(0, space), kMaxSeconds, seconds) ||
        !ParseUnsigned(arguments.substr(space + 1), 60, attenuation_db)) {
        return false;
    }
    if (seconds == 0) {
        stop_requested.store(true);
        return true;
    }
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) return false;
    stop_requested.store(false);
    // Open here so the reply reports whether echo will really play; a busy speaker fails.
    if (!output.OpenForOwner(kOwner, kSampleRate, 1, 16)) {
        ESP_LOGW(TAG, "Echo playback unavailable: output busy");
        running.store(false);
        return false;
    }
    request.output = &output;
    request.seconds = seconds;
    // The level argument is attenuation below full scale, so 12 means about -12 dBFS peaks.
    request.amplitude = std::pow(10.0F, -static_cast<float>(attenuation_db) / 20.0F);
    if (xTaskCreatePinnedToCore(EchoTask, "wake_echo", 4096, nullptr, 3, nullptr, 1) != pdPASS) {
        output.CloseForOwner(kOwner);
        running.store(false);
        return false;
    }
    return true;
}

void StopWakeEchoDiagnostic(AudioOutputService& output) {
    if (!running.load()) return;
    stop_requested.store(true);
    // Release the speaker now so the caller's open succeeds; the task sees its next write fail.
    output.CloseForOwner(kOwner);
    for (int i = 0; i < 50 && running.load(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
}

bool IsWakeEchoDiagnosticRunning() { return running.load(); }

}  // namespace rodakos
