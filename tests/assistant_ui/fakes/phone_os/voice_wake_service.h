#pragma once
#include "phone_os/voice_identity.h"
#include <string>
namespace rodakos {
enum class VoiceWakeStatus { kDisabled, kListening, kAssistantActive, kUnavailable, kError };
struct VoiceWakeState {
    bool enabled = false;
    bool runtime_available = true;
    VoiceWakeStatus status = VoiceWakeStatus::kDisabled;
    std::string runtime_name = "host-wake";
    std::string message;
    VoiceIdentityConfig voice_identity{"罗达克", "你好达克", "ni hao da ke"};
};
class VoiceWakeService {
public:
    bool Init() { return true; }
    bool SetEnabled(bool enabled) { state.enabled = enabled; ++changes; return true; }
    VoiceWakeState GetState() { return state; }
    VoiceWakeState state;
    unsigned changes = 0;
};
}
