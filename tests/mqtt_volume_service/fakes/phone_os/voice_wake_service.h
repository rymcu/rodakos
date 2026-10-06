#pragma once
#include "phone_os/voice_identity.h"
#include <mutex>
namespace rodakos {
enum class VoiceWakeStatus { kDisabled, kAssistantActive };
struct VoiceWakeState {
    VoiceWakeStatus status = VoiceWakeStatus::kDisabled;
    VoiceIdentityConfig voice_identity;
    std::string voice_identity_status;
    std::string runtime_name;
    std::string voice_identity_error;
    uint32_t voice_identity_revision_watermark = 1;
    bool voice_identity_active_confirmed = true;
};
class VoiceWakeService {
public:
    unsigned identity_calls = 0;
    unsigned rejected_identity_calls = 0;
    VoiceWakeState GetState() {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }
    void SetState(const VoiceWakeState& state) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = state;
    }
    bool ApplyVoiceIdentity(const VoiceIdentityConfig& config, std::string&) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++identity_calls;
        state_.voice_identity = config;
        state_.voice_identity_revision_watermark = config.revision;
        state_.voice_identity_status = "applied";
        state_.voice_identity_active_confirmed = true;
        state_.voice_identity_error.clear();
        return true;
    }
    void RejectVoiceIdentity(const std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++rejected_identity_calls;
        if (state_.voice_identity_status != "recovery_failed") {
            state_.voice_identity_status = "rejected";
            state_.voice_identity_error = error;
        }
    }
private:
    std::mutex mutex_;
    VoiceWakeState state_{VoiceWakeStatus::kDisabled, DefaultVoiceIdentityConfig(),
        "applied", "host-wake", "", 1, true};
};
}
