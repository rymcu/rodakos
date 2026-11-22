#pragma once
#include <string>
namespace rodakos {
enum class VoiceAssistantPhase { kIdle, kConnecting, kListening, kSpeaking, kError };
enum class VoiceAssistantTrigger { kManual, kWakeWord, kRemote };
struct VoiceAssistantState {
    VoiceAssistantPhase phase = VoiceAssistantPhase::kIdle;
    VoiceAssistantTrigger trigger = VoiceAssistantTrigger::kManual;
    bool transport_active = false;
    bool recorder_active = false;
    std::string message;
    std::string transport_name;
    std::string recorder_name;
};
class VoiceAssistantService {
public:
    VoiceAssistantState GetState() { return state; }
    VoiceAssistantState state;
};
}  // namespace rodakos
