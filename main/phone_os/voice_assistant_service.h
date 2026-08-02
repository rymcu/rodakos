#pragma once

#include "phone_os/audio_focus_service.h"
#include "phone_os/voice_assistant_transport.h"
#include "phone_os/voice_recorder_service.h"

#include <cstdint>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace rodakos {

class AudioOutputService;
class VoiceAssistantAudioCodec;

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
};

class VoiceAssistantService {
public:
    VoiceAssistantService(AudioFocusService& audio_focus,
                          VoiceAssistantTransport& transport,
                          VoiceRecorderService& recorder,
                          AudioOutputService& audio_output);
    ~VoiceAssistantService();

    bool Init();
    void Deinit();

    bool StartInteraction(VoiceAssistantTrigger trigger = VoiceAssistantTrigger::kManual,
                          const std::string& wake_word = "",
                          std::function<bool()> can_start = {},
                          uint32_t* started_generation = nullptr);
    void StopInteraction();
    void StopInteractionIfCurrent(uint32_t generation);

    void MarkConnecting(const char* message = nullptr);
    void MarkListening(const char* message = nullptr);
    void MarkSpeaking(const char* message = nullptr);
    void MarkError(const std::string& message);

    VoiceAssistantState GetState();

private:
    void SetPhaseLocked(VoiceAssistantPhase phase, const char* message);
    bool IsInteractionCurrent(uint32_t generation);
    void FinishInteraction(VoiceAssistantPhase final_phase,
                           const std::string& message,
                           uint32_t expected_generation = 0);
    void CompleteInteractionCleanupLocked(uint32_t generation);
    void CompleteStartAttempt(TaskHandle_t task);
    void WaitForCleanupComplete();
    void ReleaseFocusIfNeeded(uint32_t token, bool should_release);
    bool OpenTransportForInteraction(VoiceAssistantTrigger trigger,
                                     const std::string& wake_word,
                                     uint32_t generation,
                                     uint32_t& transport_generation);
    bool StartRecorderForInteraction(uint32_t generation);
    void StopRecorderIfNeeded(bool should_stop);
    static void IoTaskEntry(void* arg);
    bool StartIoTask();
    void WaitForIoTaskStop();
    void IoTask();
    void HandleInbound(VoiceInboundEvent&& event);
    void ProcessInbound(VoiceInboundEvent&& event);
    bool SendNextAudioFrame();
    void StopRecorderForPlayback();
    void RecordPlaybackFrame(int frame_duration_ms);
    void DrainPlayback();

    AudioFocusService& audio_focus_;
    VoiceAssistantTransport& transport_;
    VoiceRecorderService& recorder_;
    AudioOutputService& audio_output_;
    std::unique_ptr<VoiceAssistantAudioCodec> audio_codec_;
    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t io_task_ = nullptr;
    bool io_running_ = false;
    int64_t playback_deadline_us_ = 0;
    std::deque<VoiceInboundEvent> inbound_events_;
    size_t inbound_event_bytes_ = 0;
    bool initialized_ = false;
    bool deinitializing_ = false;
    bool focus_active_ = false;
    bool transport_active_ = false;
    bool recorder_active_ = false;
    bool stopping_ = false;
    bool start_in_progress_ = false;
    bool cleanup_resources_released_ = false;
    uint32_t focus_token_ = 0;
    uint32_t transport_generation_ = 0;
    uint32_t interaction_generation_ = 0;
    uint32_t cleanup_generation_ = 0;
    TaskHandle_t start_task_ = nullptr;
    TaskHandle_t cleanup_task_ = nullptr;
    VoiceAssistantPhase cleanup_final_phase_ = VoiceAssistantPhase::kIdle;
    std::string cleanup_message_ = "Ready";
    VoiceAssistantPhase phase_ = VoiceAssistantPhase::kIdle;
    VoiceAssistantTrigger trigger_ = VoiceAssistantTrigger::kManual;
    std::string message_ = "Ready";
    std::string last_wake_word_;
};

}  // namespace rodakos
