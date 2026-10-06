#pragma once

#include "phone_os/cloud_diagnostic.h"

#include <cstdint>
#include <string>

namespace rodakos {

enum class VoiceAssistantPhase {
    kIdle,
    kConnecting,
    kListening,
    kSpeaking,
    kError,
};

enum class VoiceAssistantTrigger {
    kManual,
    kWakeWord,
    kRemote,
};

struct VoiceAssistantState {
    VoiceAssistantPhase phase = VoiceAssistantPhase::kIdle;
    VoiceAssistantTrigger trigger = VoiceAssistantTrigger::kManual;
    bool initialized = false;
    bool stopping = false;
    bool focus_active = false;
    bool transport_active = false;
    bool recorder_active = false;
    uint32_t focus_token = 0;
    std::string message;
    std::string last_wake_word;
    std::string transport_name;
    std::string recorder_name;
    CloudDiagnosticCode diagnostic = CloudDiagnosticCode::kReady;
    int64_t diagnostic_at_ms = 0;
    uint32_t diagnostic_revision = 0;
};

}  // namespace rodakos
