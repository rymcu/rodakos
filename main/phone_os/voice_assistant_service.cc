#include "phone_os/voice_assistant_service.h"

#include "phone_os/audio_output_service.h"

#include <algorithm>
#include <inttypes.h>
#include <utility>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/idf_additions.h>
#include <opus_decoder.h>
#include <opus_encoder.h>

namespace rodakos {

class VoiceAssistantAudioCodec {
public:
    bool Encode(VoicePcmFrame&& frame, VoiceAudioPacket& packet) {
        const auto& config = frame.config;
        if (encoder_ == nullptr ||
            encoder_sample_rate_ != config.sample_rate ||
            encoder_channels_ != config.channels ||
            encoder_frame_duration_ms_ != config.frame_duration_ms) {
            encoder_ = std::make_unique<OpusEncoderWrapper>(
                config.sample_rate, config.channels, config.frame_duration_ms);
            encoder_->SetComplexity(0);
            encoder_sample_rate_ = config.sample_rate;
            encoder_channels_ = config.channels;
            encoder_frame_duration_ms_ = config.frame_duration_ms;
        }

        packet.sample_rate = config.sample_rate;
        packet.frame_duration_ms = config.frame_duration_ms;
        packet.timestamp_ms = frame.timestamp_ms;
        bool encoded = false;
        encoder_->Encode(std::move(frame.samples), [&](std::vector<uint8_t>&& payload) {
            packet.payload = std::move(payload);
            encoded = !packet.payload.empty();
        });
        return encoded;
    }

    bool Decode(VoiceAudioPacket&& packet, std::vector<int16_t>& pcm) {
        if (packet.sample_rate <= 0 || packet.frame_duration_ms <= 0) {
            return false;
        }
        if (decoder_ == nullptr ||
            decoder_sample_rate_ != packet.sample_rate ||
            decoder_frame_duration_ms_ != packet.frame_duration_ms) {
            decoder_ = std::make_unique<OpusDecoderWrapper>(
                packet.sample_rate, 1, packet.frame_duration_ms);
            decoder_sample_rate_ = packet.sample_rate;
            decoder_frame_duration_ms_ = packet.frame_duration_ms;
        }
        return decoder_->Decode(std::move(packet.payload), pcm);
    }

    void Reset() {
        encoder_.reset();
        decoder_.reset();
        encoder_sample_rate_ = 0;
        encoder_channels_ = 0;
        encoder_frame_duration_ms_ = 0;
        decoder_sample_rate_ = 0;
        decoder_frame_duration_ms_ = 0;
    }

private:
    std::unique_ptr<OpusEncoderWrapper> encoder_;
    std::unique_ptr<OpusDecoderWrapper> decoder_;
    int encoder_sample_rate_ = 0;
    int encoder_channels_ = 0;
    int encoder_frame_duration_ms_ = 0;
    int decoder_sample_rate_ = 0;
    int decoder_frame_duration_ms_ = 0;
};

namespace {
constexpr const char* TAG = "VoiceAssistantService";
constexpr const char* kFocusOwner = "voice-assistant";
constexpr size_t kMaxInboundEvents = 64;
constexpr size_t kMaxInboundEventBytes = 128 * 1024;
constexpr uint32_t kIoTaskStackBytes = 48 * 1024;
constexpr UBaseType_t kIoTaskPriority = 4;
// 100 Hz 下 5 ms 会折算为 0 tick，导致空转并饿死 IDLE1。
constexpr TickType_t kIoIdleDelay = 1;
constexpr int64_t kPlaybackDrainMarginUs = 20 * 1000;
constexpr int64_t kMaxPlaybackDrainUs = 2 * 1000 * 1000;

class ScopeExit {
public:
    explicit ScopeExit(std::function<void()> callback) : callback_(std::move(callback)) {}
    ~ScopeExit() {
        if (callback_) {
            callback_();
        }
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    std::function<void()> callback_;
};

size_t InboundEventBytes(const VoiceInboundEvent& event) {
    return event.audio.payload.size() + event.payload.size();
}

bool IsDroppableInboundEvent(const VoiceInboundEvent& event) {
    return event.type == VoiceInboundEventType::kAudio ||
           event.type == VoiceInboundEventType::kMcp;
}

const char* TriggerName(VoiceAssistantTrigger trigger) {
    switch (trigger) {
        case VoiceAssistantTrigger::kWakeWord:
            return "wake-word";
        case VoiceAssistantTrigger::kRemote:
            return "remote";
        case VoiceAssistantTrigger::kManual:
        default:
            return "manual";
    }
}

}  // namespace

VoiceAssistantService::VoiceAssistantService(AudioFocusService& audio_focus,
                                             VoiceAssistantTransport& transport,
                                             VoiceRecorderService& recorder,
                                             AudioOutputService& audio_output)
    : audio_focus_(audio_focus),
      transport_(transport),
      recorder_(recorder),
      audio_output_(audio_output),
      audio_codec_(std::make_unique<VoiceAssistantAudioCodec>()) {
    mutex_ = xSemaphoreCreateMutex();
    transport_.SetInboundHandler([this](VoiceInboundEvent&& event) {
        HandleInbound(std::move(event));
    });
}

VoiceAssistantService::~VoiceAssistantService() {
    Deinit();
    transport_.SetInboundHandler({});
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool VoiceAssistantService::Init() {
    if (mutex_ == nullptr) {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (deinitializing_) {
        xSemaphoreGive(mutex_);
        return false;
    }
    if (!initialized_) {
        recorder_.Init();
        initialized_ = true;
        phase_ = VoiceAssistantPhase::kIdle;
        message_ = "Ready";
    }
    xSemaphoreGive(mutex_);
    return true;
}

void VoiceAssistantService::Deinit() {
    if (mutex_ == nullptr) {
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (deinitializing_) {
        xSemaphoreGive(mutex_);
        while (true) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            const bool complete = !deinitializing_;
            xSemaphoreGive(mutex_);
            if (complete) {
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    deinitializing_ = true;
    xSemaphoreGive(mutex_);

    StopInteraction();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    initialized_ = false;
    phase_ = VoiceAssistantPhase::kIdle;
    message_ = "Stopped";
    xSemaphoreGive(mutex_);
    recorder_.Deinit();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    deinitializing_ = false;
    xSemaphoreGive(mutex_);
}

bool VoiceAssistantService::StartInteraction(VoiceAssistantTrigger trigger,
                                              const std::string& wake_word,
                                              std::function<bool()> can_start,
                                              uint32_t* started_generation) {
    if (mutex_ == nullptr) {
        return false;
    }
    if (started_generation != nullptr) {
        *started_generation = 0;
    }

    const TaskHandle_t start_task = xTaskGetCurrentTaskHandle();
    uint32_t interaction_generation = 0;
    uint32_t active_transport_generation = 0;
    bool already_active = false;
    bool interaction_busy = true;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!deinitializing_ && !initialized_) {
        recorder_.Init();
        initialized_ = true;
        phase_ = VoiceAssistantPhase::kIdle;
        message_ = "Ready";
    }
    if (trigger != VoiceAssistantTrigger::kWakeWord) {
        if (!stopping_ && phase_ == VoiceAssistantPhase::kIdle) {
            message_ = "Waiting for wake word";
        }
        xSemaphoreGive(mutex_);
        return false;
    }
    already_active = !stopping_ && focus_active_ && transport_active_;
    interaction_busy = deinitializing_ || !initialized_ || stopping_ || start_in_progress_ ||
                       (!already_active &&
                        (io_task_ != nullptr ||
                         (phase_ != VoiceAssistantPhase::kIdle &&
                          phase_ != VoiceAssistantPhase::kError)));
    if (!interaction_busy) {
        start_in_progress_ = true;
        start_task_ = start_task;
        interaction_generation = interaction_generation_;
    }
    if (!interaction_busy && already_active) {
        trigger_ = trigger;
        last_wake_word_ = wake_word;
        active_transport_generation = transport_generation_;
    }
    xSemaphoreGive(mutex_);

    if (interaction_busy) {
        return false;
    }
    ScopeExit complete_start([this, start_task]() {
        CompleteStartAttempt(start_task);
    });

    if (already_active) {
        if (trigger == VoiceAssistantTrigger::kWakeWord && !wake_word.empty()) {
            if (!transport_.SendWakeWordDetected(wake_word, active_transport_generation)) {
                FinishInteraction(
                    VoiceAssistantPhase::kError, transport_.last_error(), interaction_generation);
                return false;
            }
        }
        if (!transport_.SendStartListening(
                VoiceListeningMode::kAutoStop, active_transport_generation)) {
            FinishInteraction(
                VoiceAssistantPhase::kError, transport_.last_error(), interaction_generation);
            return false;
        }
        MarkListening("Listening");
        if (started_generation != nullptr) {
            *started_generation = interaction_generation;
        }
        return true;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!initialized_ || deinitializing_ || stopping_ || io_task_ != nullptr ||
        (phase_ != VoiceAssistantPhase::kIdle && phase_ != VoiceAssistantPhase::kError)) {
        xSemaphoreGive(mutex_);
        return false;
    }
    trigger_ = trigger;
    last_wake_word_ = wake_word;
    playback_deadline_us_ = 0;
    interaction_generation = ++interaction_generation_;
    SetPhaseLocked(VoiceAssistantPhase::kConnecting, "Connecting");
    xSemaphoreGive(mutex_);

    if (can_start && !can_start()) {
        FinishInteraction(VoiceAssistantPhase::kIdle, "Ready", interaction_generation);
        return false;
    }

    // Start capture first so speech immediately after the wake phrase is retained while
    // the I/O task, focus, and WebSocket are being prepared.
    if (!StartRecorderForInteraction(interaction_generation)) {
        FinishInteraction(
            VoiceAssistantPhase::kError, recorder_.last_error(), interaction_generation);
        return false;
    }
    if (!IsInteractionCurrent(interaction_generation)) {
        return false;
    }

    // Keep this stack in PSRAM so the WebSocket task can reserve scarce internal SRAM.
    // transport_active_ remains false until the listen handshake is committed.
    if (!StartIoTask()) {
        FinishInteraction(
            VoiceAssistantPhase::kError, "Assistant audio task unavailable", interaction_generation);
        return false;
    }

    AudioFocusRequest request;
    request.owner = kFocusOwner;
    request.gain = AudioFocusGain::kExclusive;
    request.resume_on_release = true;
    request.release_playback_hardware = true;

    uint32_t token = 0;
    if (!audio_focus_.RequestFocus(request, token)) {
        FinishInteraction(
            VoiceAssistantPhase::kError, "Audio focus unavailable", interaction_generation);
        return false;
    }

    bool focus_accepted = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (interaction_generation_ == interaction_generation &&
        initialized_ && !deinitializing_ && !stopping_ &&
        phase_ == VoiceAssistantPhase::kConnecting) {
        focus_active_ = true;
        focus_token_ = token;
        focus_accepted = true;
    }
    xSemaphoreGive(mutex_);
    if (!focus_accepted) {
        audio_focus_.ReleaseFocus(token);
        recorder_.Stop();
        return false;
    }

    uint32_t opened_transport_generation = 0;
    if (!OpenTransportForInteraction(
            trigger, wake_word, interaction_generation, opened_transport_generation)) {
        FinishInteraction(
            VoiceAssistantPhase::kError, transport_.last_error(), interaction_generation);
        return false;
    }
    if (!IsInteractionCurrent(interaction_generation)) {
        return false;
    }
    if (!transport_.SendStartListening(
            VoiceListeningMode::kAutoStop, opened_transport_generation)) {
        FinishInteraction(
            VoiceAssistantPhase::kError, transport_.last_error(), interaction_generation);
        return false;
    }
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (interaction_generation_ == interaction_generation && !stopping_ && focus_active_) {
            SetPhaseLocked(
                VoiceAssistantPhase::kListening,
                trigger == VoiceAssistantTrigger::kWakeWord ? "Wake word accepted" : "Listening");
            transport_active_ = true;
        }
        xSemaphoreGive(mutex_);
    }

    if (!IsInteractionCurrent(interaction_generation)) {
        return false;
    }

    ESP_LOGI(TAG, "Interaction started: trigger=%s focus_token=%" PRIu32,
             TriggerName(trigger), token);
    if (started_generation != nullptr) {
        *started_generation = interaction_generation;
    }
    return true;
}

void VoiceAssistantService::StopInteraction() {
    FinishInteraction(VoiceAssistantPhase::kIdle, "Ready");
}

void VoiceAssistantService::StopInteractionIfCurrent(uint32_t generation) {
    FinishInteraction(VoiceAssistantPhase::kIdle, "Ready", generation);
}

void VoiceAssistantService::MarkConnecting(const char* message) {
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!stopping_) {
        SetPhaseLocked(
            VoiceAssistantPhase::kConnecting, message != nullptr ? message : "Connecting");
    }
    xSemaphoreGive(mutex_);
}

void VoiceAssistantService::MarkListening(const char* message) {
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!stopping_) {
        SetPhaseLocked(
            VoiceAssistantPhase::kListening, message != nullptr ? message : "Listening");
    }
    xSemaphoreGive(mutex_);
}

void VoiceAssistantService::MarkSpeaking(const char* message) {
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!stopping_) {
        SetPhaseLocked(
            VoiceAssistantPhase::kSpeaking, message != nullptr ? message : "Speaking");
    }
    xSemaphoreGive(mutex_);
}

void VoiceAssistantService::MarkError(const std::string& message) {
    FinishInteraction(
        VoiceAssistantPhase::kError, message.empty() ? "Error" : message);
}

VoiceAssistantState VoiceAssistantService::GetState() {
    VoiceAssistantState state;
    if (mutex_ == nullptr) {
        return state;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    state.phase = phase_;
    state.trigger = trigger_;
    state.initialized = initialized_;
    state.stopping = stopping_;
    state.focus_active = focus_active_;
    state.transport_active = transport_active_;
    state.recorder_active = recorder_active_;
    state.focus_token = focus_token_;
    state.message = message_;
    state.last_wake_word = last_wake_word_;
    state.transport_name = transport_.name();
    state.recorder_name = recorder_.name();
    xSemaphoreGive(mutex_);
    return state;
}

void VoiceAssistantService::SetPhaseLocked(VoiceAssistantPhase phase, const char* message) {
    phase_ = phase;
    if (message != nullptr) {
        message_ = message;
    }
}

bool VoiceAssistantService::IsInteractionCurrent(uint32_t generation) {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool current = initialized_ && !deinitializing_ &&
                         interaction_generation_ == generation &&
                         !stopping_ &&
                         phase_ != VoiceAssistantPhase::kIdle &&
                         phase_ != VoiceAssistantPhase::kError;
    xSemaphoreGive(mutex_);
    return current;
}

void VoiceAssistantService::FinishInteraction(VoiceAssistantPhase final_phase,
                                               const std::string& message,
                                               uint32_t expected_generation) {
    if (mutex_ == nullptr) {
        return;
    }

    const TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    uint32_t cleanup_generation = 0;
    uint32_t transport_generation = 0;
    uint32_t token = 0;
    bool should_release = false;
    bool should_stop_transport = false;
    bool should_stop_recorder = false;
    bool called_from_io_task = false;
    bool called_from_start_task = false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (expected_generation != 0 && interaction_generation_ != expected_generation) {
        xSemaphoreGive(mutex_);
        return;
    }
    if (stopping_) {
        const bool same_cleanup_task = cleanup_task_ == current_task;
        const bool called_from_io_task = io_task_ != nullptr && io_task_ == current_task;
        const bool called_from_start_task = start_in_progress_ && start_task_ == current_task;
        xSemaphoreGive(mutex_);
        if (!same_cleanup_task && !called_from_io_task && !called_from_start_task) {
            WaitForCleanupComplete();
        }
        return;
    }

    stopping_ = true;
    cleanup_resources_released_ = false;
    cleanup_task_ = current_task;
    cleanup_generation = ++interaction_generation_;
    cleanup_generation_ = cleanup_generation;
    cleanup_final_phase_ = final_phase;
    cleanup_message_ = message.empty() ? "Ready" : message;
    token = focus_token_;
    should_release = focus_active_;
    should_stop_transport = transport_active_;
    should_stop_recorder = recorder_active_;
    transport_generation = transport_generation_;
    called_from_io_task = io_task_ != nullptr && io_task_ == current_task;
    called_from_start_task = start_in_progress_ && start_task_ == current_task;
    io_running_ = false;
    inbound_events_.clear();
    inbound_event_bytes_ = 0;
    transport_generation_ = 0;
    message_ = "Stopping";
    xSemaphoreGive(mutex_);

    // Release capture first. Network shutdown may wait for the websocket task, but
    // disabling wake must stop ADC ownership immediately.
    StopRecorderIfNeeded(should_stop_recorder);
    if (!called_from_io_task) {
        WaitForIoTaskStop();
    }

    audio_output_.CloseForOwner(kFocusOwner);
    if (audio_codec_ != nullptr) {
        audio_codec_->Reset();
    }
    ReleaseFocusIfNeeded(token, should_release);
    if (should_stop_transport) {
        transport_.SendStopListening(transport_generation);
    }
    transport_.CloseAudioChannel();
    transport_.WaitForAudioChannelClosed();

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (stopping_ && cleanup_generation_ == cleanup_generation) {
        focus_active_ = false;
        transport_active_ = false;
        recorder_active_ = false;
        focus_token_ = 0;
        playback_deadline_us_ = 0;
        cleanup_resources_released_ = true;
        CompleteInteractionCleanupLocked(cleanup_generation);
    }
    xSemaphoreGive(mutex_);
    if (!called_from_io_task && !called_from_start_task) {
        WaitForCleanupComplete();
    }
}

void VoiceAssistantService::CompleteInteractionCleanupLocked(uint32_t generation) {
    if (!stopping_ || start_in_progress_ || !cleanup_resources_released_ ||
        io_task_ != nullptr ||
        cleanup_generation_ != generation) {
        return;
    }

    SetPhaseLocked(cleanup_final_phase_, cleanup_message_.c_str());
    stopping_ = false;
    cleanup_resources_released_ = false;
    cleanup_generation_ = 0;
    cleanup_task_ = nullptr;
}

void VoiceAssistantService::CompleteStartAttempt(TaskHandle_t task) {
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (start_in_progress_ && start_task_ == task) {
        start_in_progress_ = false;
        start_task_ = nullptr;
        if (stopping_ && cleanup_resources_released_) {
            CompleteInteractionCleanupLocked(cleanup_generation_);
        }
    }
    xSemaphoreGive(mutex_);
}

void VoiceAssistantService::WaitForCleanupComplete() {
    if (mutex_ == nullptr) {
        return;
    }
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool complete = !stopping_;
        xSemaphoreGive(mutex_);
        if (complete) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void VoiceAssistantService::ReleaseFocusIfNeeded(uint32_t token, bool should_release) {
    if (should_release && token != 0) {
        audio_focus_.ReleaseFocus(token);
        ESP_LOGI(TAG, "Interaction stopped: focus_token=%" PRIu32, token);
    }
}

bool VoiceAssistantService::OpenTransportForInteraction(VoiceAssistantTrigger trigger,
                                                        const std::string& wake_word,
                                                        uint32_t generation,
                                                        uint32_t& transport_generation) {
    transport_generation = 0;
    if (!transport_.Start()) {
        return false;
    }
    if (!transport_.OpenAudioChannel([this, generation]() {
            return IsInteractionCurrent(generation);
        })) {
        return false;
    }
    if (!IsInteractionCurrent(generation)) {
        transport_.CloseAudioChannel();
        return false;
    }
    const uint32_t opened_transport_generation = transport_.connection_generation();
    if (opened_transport_generation == 0) {
        transport_.CloseAudioChannel();
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool current = initialized_ && !deinitializing_ &&
                         interaction_generation_ == generation && !stopping_ &&
                         phase_ == VoiceAssistantPhase::kConnecting;
    if (current) {
        transport_generation_ = opened_transport_generation;
        transport_generation = opened_transport_generation;
    }
    xSemaphoreGive(mutex_);
    if (!current) {
        transport_.CloseAudioChannel();
        return false;
    }
    if (trigger == VoiceAssistantTrigger::kWakeWord && !wake_word.empty() &&
        !transport_.SendWakeWordDetected(wake_word, opened_transport_generation)) {
        transport_.CloseAudioChannel();
        return false;
    }
    return true;
}

bool VoiceAssistantService::StartRecorderForInteraction(uint32_t generation) {
    VoiceRecorderConfig config;
    config.frame_duration_ms = 60;
    if (!recorder_.Start(config)) {
        return false;
    }

    bool current = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        current = interaction_generation_ == generation &&
                  initialized_ && !deinitializing_ &&
                  !stopping_ &&
                  phase_ == VoiceAssistantPhase::kConnecting;
        if (current) {
            recorder_active_ = true;
        }
        xSemaphoreGive(mutex_);
    }
    if (!current) {
        recorder_.Stop();
    }
    return current;
}

void VoiceAssistantService::StopRecorderIfNeeded(bool should_stop) {
    if (should_stop) {
        recorder_.Stop();
    }
}

void VoiceAssistantService::IoTaskEntry(void* arg) {
    static_cast<VoiceAssistantService*>(arg)->IoTask();
}

bool VoiceAssistantService::StartIoTask() {
    if (mutex_ == nullptr) {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!initialized_ || deinitializing_ || stopping_ ||
        phase_ != VoiceAssistantPhase::kConnecting) {
        xSemaphoreGive(mutex_);
        return false;
    }
    inbound_events_.clear();
    inbound_event_bytes_ = 0;
    io_running_ = true;
    if (io_task_ != nullptr) {
        xSemaphoreGive(mutex_);
        return true;
    }

#if CONFIG_SOC_CPU_CORES_NUM > 1
    const BaseType_t created = xTaskCreatePinnedToCoreWithCaps(
        IoTaskEntry, "assistant_io", kIoTaskStackBytes, this,
        kIoTaskPriority, &io_task_, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    const BaseType_t created = xTaskCreateWithCaps(
        IoTaskEntry, "assistant_io", kIoTaskStackBytes, this,
        kIoTaskPriority, &io_task_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (created != pdPASS) {
        const unsigned internal_free = static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        const unsigned internal_largest = static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        const unsigned psram_free = static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        const unsigned psram_largest = static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        ESP_LOGE(TAG,
                 "Failed to create assistant I/O task in PSRAM: internal_free=%u "
                 "internal_largest=%u psram_free=%u psram_largest=%u stack=%u",
                 internal_free, internal_largest, psram_free, psram_largest,
                 static_cast<unsigned>(kIoTaskStackBytes));
        io_running_ = false;
        io_task_ = nullptr;
        xSemaphoreGive(mutex_);
        return false;
    }
    ESP_LOGI(TAG,
             "Assistant I/O task ready in PSRAM: internal_free=%u largest=%u",
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    xSemaphoreGive(mutex_);
    return true;
}

void VoiceAssistantService::WaitForIoTaskStop() {
    if (mutex_ == nullptr) {
        return;
    }
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool stopped = io_task_ == nullptr;
        xSemaphoreGive(mutex_);
        if (stopped) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void VoiceAssistantService::IoTask() {
    bool first_audio_frame_logged = false;
    while (true) {
        VoiceInboundEvent event;
        bool has_event = false;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool running = io_running_;
        if (running && transport_active_ && !inbound_events_.empty()) {
            event = std::move(inbound_events_.front());
            inbound_event_bytes_ -= std::min(inbound_event_bytes_, InboundEventBytes(event));
            inbound_events_.pop_front();
            has_event = true;
        }
        xSemaphoreGive(mutex_);

        if (!running) {
            break;
        }
        if (has_event) {
            ProcessInbound(std::move(event));
            continue;
        }
        if (!SendNextAudioFrame()) {
            vTaskDelay(kIoIdleDelay);
        } else if (!first_audio_frame_logged) {
            first_audio_frame_logged = true;
            ESP_LOGI(TAG,
                     "Assistant I/O first audio frame sent: stack_min_free=%u bytes",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) *
                                           sizeof(StackType_t)));
        }
    }

    const unsigned stack_min_free = static_cast<unsigned>(
        uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t));
    ESP_LOGI(TAG,
             "Assistant I/O task stopping: stack_min_free=%u bytes",
             stack_min_free);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    io_task_ = nullptr;
    if (stopping_ && cleanup_resources_released_) {
        CompleteInteractionCleanupLocked(cleanup_generation_);
    }
    xSemaphoreGive(mutex_);
    vTaskDeleteWithCaps(nullptr);
}

void VoiceAssistantService::HandleInbound(VoiceInboundEvent&& event) {
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (io_running_ && transport_generation_ != 0 &&
        event.transport_generation == transport_generation_) {
        const size_t event_bytes = InboundEventBytes(event);
        if (event_bytes > kMaxInboundEventBytes && IsDroppableInboundEvent(event)) {
            xSemaphoreGive(mutex_);
            return;
        }

        while (!inbound_events_.empty() &&
               (inbound_events_.size() >= kMaxInboundEvents ||
                inbound_event_bytes_ + event_bytes > kMaxInboundEventBytes)) {
            auto drop = std::find_if(inbound_events_.begin(),
                                     inbound_events_.end(),
                                     IsDroppableInboundEvent);
            if (drop == inbound_events_.end()) {
                if (IsDroppableInboundEvent(event)) {
                    xSemaphoreGive(mutex_);
                    return;
                }
                drop = inbound_events_.begin();
            }
            inbound_event_bytes_ -=
                std::min(inbound_event_bytes_, InboundEventBytes(*drop));
            inbound_events_.erase(drop);
        }
        inbound_event_bytes_ += event_bytes;
        inbound_events_.push_back(std::move(event));
    }
    xSemaphoreGive(mutex_);
}

void VoiceAssistantService::ProcessInbound(VoiceInboundEvent&& event) {
    switch (event.type) {
        case VoiceInboundEventType::kSpeakingStarted:
            StopRecorderForPlayback();
            MarkSpeaking("Speaking");
            break;
        case VoiceInboundEventType::kAudio: {
            StopRecorderForPlayback();
            MarkSpeaking("Speaking");
            const int sample_rate = event.audio.sample_rate;
            const int frame_duration_ms = event.audio.frame_duration_ms;
            std::vector<int16_t> pcm;
            if (audio_codec_ == nullptr || !audio_codec_->Decode(std::move(event.audio), pcm)) {
                MarkError("Failed to decode assistant audio");
                break;
            }
            if (!audio_output_.IsOpenForOwner(kFocusOwner) &&
                !audio_output_.OpenForOwner(
                    kFocusOwner, static_cast<uint32_t>(sample_rate), 1, 16)) {
                MarkError("Assistant audio output unavailable");
                break;
            }
            if (!pcm.empty() &&
                !audio_output_.WriteForOwner(
                    kFocusOwner, pcm.data(), static_cast<int>(pcm.size() * sizeof(int16_t)))) {
                MarkError("Assistant audio playback failed");
            } else if (!pcm.empty()) {
                RecordPlaybackFrame(frame_duration_ms);
            }
            break;
        }
        case VoiceInboundEventType::kSpeakingStopped:
            DrainPlayback();
            StopInteraction();
            break;
        case VoiceInboundEventType::kSessionFinished:
            DrainPlayback();
            StopInteraction();
            break;
        case VoiceInboundEventType::kMcp:
            ESP_LOGI(TAG, "Received assistant MCP message (%u bytes)",
                     static_cast<unsigned>(event.payload.size()));
            break;
        case VoiceInboundEventType::kError:
            MarkError(event.payload.empty() ? "Voice transport failed" : event.payload.c_str());
            break;
    }
}

bool VoiceAssistantService::SendNextAudioFrame() {
    uint32_t transport_generation = 0;
    bool transport_active = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    transport_generation = transport_generation_;
    transport_active = transport_active_;
    xSemaphoreGive(mutex_);
    if (!transport_active || transport_generation == 0) {
        return false;
    }

    VoicePcmFrame frame;
    if (!recorder_.PopFrame(frame)) {
        return false;
    }

    VoiceAudioPacket packet;
    if (audio_codec_ == nullptr || !audio_codec_->Encode(std::move(frame), packet)) {
        return true;
    }
    if (!transport_.SendAudio(packet, transport_generation)) {
        MarkError(transport_.last_error());
    }
    return true;
}

void VoiceAssistantService::StopRecorderForPlayback() {
    bool should_stop = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        should_stop = recorder_active_;
        recorder_active_ = false;
        xSemaphoreGive(mutex_);
    }
    StopRecorderIfNeeded(should_stop);
}

void VoiceAssistantService::RecordPlaybackFrame(int frame_duration_ms) {
    if (mutex_ == nullptr || frame_duration_ms <= 0) {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    playback_deadline_us_ = std::max(playback_deadline_us_, now_us) +
                            static_cast<int64_t>(frame_duration_ms) * 1000;
    xSemaphoreGive(mutex_);
}

void VoiceAssistantService::DrainPlayback() {
    if (mutex_ == nullptr) {
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    const int64_t deadline_us = playback_deadline_us_;
    playback_deadline_us_ = 0;
    xSemaphoreGive(mutex_);

    const int64_t remaining_us = std::clamp(
        deadline_us - esp_timer_get_time() + kPlaybackDrainMarginUs,
        int64_t{0},
        kMaxPlaybackDrainUs);
    if (remaining_us > 0) {
        vTaskDelay(pdMS_TO_TICKS((remaining_us + 999) / 1000));
    }
}

}  // namespace rodakos
