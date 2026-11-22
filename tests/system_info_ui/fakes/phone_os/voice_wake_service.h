#pragma once
#include <string>
namespace rodakos {
enum class VoiceWakeStatus { kDisabled, kListening, kAssistantActive, kUnavailable, kError };
struct VoiceIdentityConfig {
    std::string wake_word;
};
struct VoiceWakeState {
    bool enabled = false;
    bool runtime_available = true;
    bool listening = false;
    VoiceWakeStatus status = VoiceWakeStatus::kDisabled;
    std::string runtime_name;
    std::string message;
    VoiceIdentityConfig voice_identity;
};
class VoiceWakeService {
public:
    VoiceWakeState GetState() { return state; }
    VoiceWakeState state;
};
}  // namespace rodakos
