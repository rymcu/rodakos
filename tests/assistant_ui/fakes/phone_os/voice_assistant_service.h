#pragma once
#include "phone_os/voice_assistant_state.h"
namespace rodakos {
class VoiceAssistantService {
public:
    bool Init() { ++initializations; return true; }
    VoiceAssistantState GetState() { return state; }
    VoiceAssistantState state;
    unsigned initializations = 0;
};
}
