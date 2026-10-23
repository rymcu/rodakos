#pragma once

#include "phone_os/audio_focus_service.h"
#include "phone_os/pcm_level_meter.h"
#include "phone_os/task-retirement.h"
#include "phone_os/voice_assistant_state.h"
#include "phone_os/voice_assistant_reconnect_coordinator.h"
#include "phone_os/voice_assistant_transport.h"
#include "phone_os/voice_conversation_policy.h"
#include "phone_os/voice_barge_in_policy.h"
#include "phone_os/voice_playback_queue.h"
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

class VoiceAssistantService {
public:
    VoiceAssistantService(AudioFocusService& audio_focus,
                          VoiceAssistantTransport& transport,
                          VoiceRecorderService& recorder,
                          AudioOutputService& audio_output);
    ~VoiceAssistantService();

    bool Init();
    void Deinit();
    // Configure once before the first interaction. Runs on the starting task after the
    // recorder starts and before the transport opens, so memory-heavy sessions can stop.
    void SetBeforeTransportCallback(std::function<void()> callback) {
        before_transport_callback_ = std::move(callback);
    }

    bool StartInteraction(VoiceAssistantTrigger trigger = VoiceAssistantTrigger::kManual,
                          const std::string& wake_word = "",
                          std::function<bool()> can_start = {},
                          uint32_t* started_generation = nullptr);
    void StopInteraction();
    void StopInteractionIfCurrent(uint32_t generation);

    // Queue confirmed user speech for the I/O task; true means accepted, not sent.
    bool InterruptSpeaking();

    void MarkConnecting(const char* message = nullptr);
    void MarkListening(const char* message = nullptr);
    void MarkSpeaking(const char* message = nullptr);
    void MarkError(const std::string& message);

    // Observation only; commands still validate their current state under the service lock.
    VoiceAssistantPhase GetPhaseSnapshot();
    VoiceAssistantState GetState();

private:
    void SetPhaseLocked(VoiceAssistantPhase phase, const char* message);
    bool IsInteractionCurrent(uint32_t generation);
    void FinishInteraction(VoiceAssistantPhase final_phase,
                           const std::string& message,
                           uint32_t expected_generation = 0,
                           CloudDiagnosticCode diagnostic = CloudDiagnosticCode::kVoiceUnavailable,
                           uint32_t expected_recording_transport_generation = 0);
    void FinishTransportFailure(const VoiceTransportFailure& failure, uint32_t expected_generation);
    void CompleteInteractionCleanupLocked(uint32_t generation);
    void CompleteStartAttempt(TaskHandle_t task);
    void WaitForCleanupComplete(uint32_t generation);
    void ReleaseFocusIfNeeded(uint32_t token, bool should_release);
    bool OpenTransportForInteraction(VoiceAssistantTrigger trigger,
                                     const std::string& wake_word,
                                     uint32_t generation,
                                     uint32_t& transport_generation);
    bool StartRecorderForInteraction(uint32_t generation);
    void StopRecorderIfNeeded(bool should_stop);
    static void IoTaskEntry(void* arg);
    bool StartIoTask();
    void IoTask();
    static void PlaybackTaskEntry(void* arg);
    bool StartPlaybackTask();
    void PlaybackTask();
    void ClearPlaybackLocked();
    void WaitForPlaybackIdle();
    void HandleInbound(VoiceInboundEvent&& event);
    void ProcessInbound(VoiceInboundEvent&& event);
    void HandleTransportFailure(VoiceTransportFailure failure);
    void ProcessReconnect(uint32_t interaction_generation);
    void QueueTransportFailure(VoiceTransportFailure failure);
    void ProcessInterrupt();
    bool SendNextAudioFrame();
    bool BeginFollowUpTurn();
    bool HasTerminalInboundEventLocked() const;
    void StopRecorderForPlayback();
    void PlayWakeChime();
    void RecordPlaybackFrame(int frame_duration_ms);
    void DrainPlayback();
    void DiscardPendingRecorderFrames();

    AudioFocusService& audio_focus_;
    VoiceAssistantTransport& transport_;
    VoiceRecorderService& recorder_;
    AudioOutputService& audio_output_;
    std::unique_ptr<VoiceAssistantAudioCodec> audio_codec_;
    std::function<void()> before_transport_callback_;
    SemaphoreHandle_t mutex_ = nullptr;
    TaskRetirementOwner io_retirement_owner_;
    TaskRetirementTicket io_retirement_;
    TaskHandle_t io_task_ = nullptr;
    bool io_running_ = false;
    // Speaker writes block on codec DMA for about one frame each. They run on their own
    // task so the I/O task keeps the microphone uplink and barge-in detection real-time.
    TaskRetirementOwner playback_retirement_owner_;
    TaskRetirementTicket playback_retirement_;
    TaskHandle_t playback_task_ = nullptr;
    bool playback_running_ = false;
    bool playback_writing_ = false;
    bool playback_end_pending_ = false;
    bool playback_failed_ = false;
    uint32_t playback_clear_generation_ = 0;
    VoicePlaybackQueue playback_queue_;
    int64_t playback_deadline_us_ = 0;
    int64_t follow_up_rearm_not_before_us_ = 0;
    std::deque<VoiceInboundEvent> inbound_events_;
    size_t inbound_event_bytes_ = 0;
    bool initialized_ = false;
    bool deinitializing_ = false;
    uint32_t deinitialization_generation_ = 0;
    bool focus_active_ = false;
    bool transport_active_ = false;
    bool recorder_active_ = false;
    bool follow_up_rearm_pending_ = false;
    bool speaking_interrupted_ = false;
    bool interrupt_pending_ = false;
    bool interrupt_manual_ = false;
    bool stopping_ = false;
    bool start_in_progress_ = false;
    bool cleanup_resources_released_ = false;
    bool reconnect_pending_ = false;
    uint32_t focus_token_ = 0;
    uint32_t transport_generation_ = 0;
    uint32_t vad_sequence_ = 0;
    uint32_t interrupt_onset_ms_ = 0;
    uint32_t vad_end_sequence_ = 0;
    uint32_t vad_end_epoch_ = 0;
    uint32_t interaction_generation_ = 0;
    uint32_t cleanup_generation_ = 0;
    TaskHandle_t start_task_ = nullptr;
    TaskHandle_t cleanup_task_ = nullptr;
    VoiceAssistantPhase cleanup_final_phase_ = VoiceAssistantPhase::kIdle;
    std::string cleanup_message_ = "Ready";
    CloudDiagnosticCode cleanup_diagnostic_ = CloudDiagnosticCode::kReady;
    VoiceConversationPolicy conversation_policy_;
    VoiceBargeInPolicy barge_in_policy_;
    VoiceVadEndPolicy vad_end_policy_;
    VoicePlaybackEpochPolicy playback_epoch_policy_;
    VoiceAssistantReconnectCoordinator reconnect_coordinator_;
    bool playback_vad_started_ = false;
    uint32_t playback_audio_events_ = 0;
    uint32_t playback_decoded_frames_ = 0;
    uint64_t playback_decoded_pcm_bytes_ = 0;
    uint32_t playback_write_failures_ = 0;
    PcmLevelMeter playback_level_;
    VoiceAssistantPhase phase_ = VoiceAssistantPhase::kIdle;
    VoiceAssistantTrigger trigger_ = VoiceAssistantTrigger::kManual;
    std::string message_ = "Ready";
    CloudDiagnosticCode diagnostic_ = CloudDiagnosticCode::kReady;
    int64_t diagnostic_at_ms_ = 0;
    uint32_t diagnostic_revision_ = 0;
    std::string last_wake_word_;
};

}  // namespace rodakos
