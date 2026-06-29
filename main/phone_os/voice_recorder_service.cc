#include "phone_os/voice_recorder_service.h"

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceRecorder";
}

bool NoopVoiceRecorderService::Init() {
    initialized_ = true;
    return true;
}

void NoopVoiceRecorderService::Deinit() {
    initialized_ = false;
}

bool NoopVoiceRecorderService::Start(const VoiceRecorderConfig&) {
    if (!initialized_) {
        Init();
    }
    last_error_ = "Voice recorder not configured";
    ESP_LOGW(TAG, "Cannot start recorder: %s", last_error_.c_str());
    return false;
}

void NoopVoiceRecorderService::Stop() {
}

bool NoopVoiceRecorderService::IsRunning() const {
    return false;
}

bool NoopVoiceRecorderService::PopFrame(VoicePcmFrame&) {
    return false;
}

}  // namespace rodakos
