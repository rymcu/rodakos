#pragma once
#include "phone_os/voice_identity.h"
namespace rodakos {
enum class VoiceWakeStatus { kDisabled, kAssistantActive };
struct VoiceWakeState {
    VoiceWakeStatus status = VoiceWakeStatus::kDisabled;
    VoiceIdentityConfig voice_identity;
    std::string voice_identity_status;
    std::string runtime_name;
    std::string voice_identity_error;
};
class VoiceWakeService {
public:
    VoiceWakeState GetState() { return {}; }
    bool ApplyVoiceIdentity(const VoiceIdentityConfig&, std::string&) { return false; }
};
}
