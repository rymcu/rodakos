#pragma once

#include "phone_os/audio_focus_service.h"
#include "phone_os/voice_assistant_transport.h"
#include "phone_os/voice_recorder_service.h"

#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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
    bool focus_active = false;
    bool transport_active = false;
    bool recorder_active = false;
    uint32_t focus_token = 0;
    std::string message;
    std::string last_wake_word;
    std::string transport_name;
    std::string recorder_name;
};

class VoiceAssistantService {
public:
    VoiceAssistantService(AudioFocusService& audio_focus,
                          VoiceAssistantTransport& transport,
                          VoiceRecorderService& recorder);
    ~VoiceAssistantService();

    bool Init();
    void Deinit();

    bool StartInteraction(VoiceAssistantTrigger trigger = VoiceAssistantTrigger::kManual,
                          const std::string& wake_word = "");
    void StopInteraction();

    void MarkConnecting(const char* message = nullptr);
    void MarkListening(const char* message = nullptr);
    void MarkSpeaking(const char* message = nullptr);
    void MarkError(const char* message);

    VoiceAssistantState GetState();

private:
    void SetPhaseLocked(VoiceAssistantPhase phase, const char* message);
    void ReleaseFocusIfNeeded(uint32_t token, bool should_release);
    bool OpenTransportForInteraction(VoiceAssistantTrigger trigger, const std::string& wake_word);
    bool StartRecorderForInteraction();
    void StopRecorderIfNeeded(bool should_stop);

    AudioFocusService& audio_focus_;
    VoiceAssistantTransport& transport_;
    VoiceRecorderService& recorder_;
    SemaphoreHandle_t mutex_ = nullptr;
    bool initialized_ = false;
    bool focus_active_ = false;
    bool transport_active_ = false;
    bool recorder_active_ = false;
    uint32_t focus_token_ = 0;
    VoiceAssistantPhase phase_ = VoiceAssistantPhase::kIdle;
    VoiceAssistantTrigger trigger_ = VoiceAssistantTrigger::kManual;
    std::string message_ = "Ready";
    std::string last_wake_word_;
};

}  // namespace rodakos
