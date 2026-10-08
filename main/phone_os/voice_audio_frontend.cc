#include "phone_os/voice_audio_frontend.h"

#include "rodakos_adapters/audio_codec_input.h"
#if defined(RODAKOS_RELEASE_TESTS)
#include "phone_os/voice_feed_progress_observer.h"
#include "phone_os/voice_tick_observer.h"
#endif

#include <algorithm>
#include <cstring>
#include <utility>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_afe_sr_models.h>
#include <esp_afe_config.h>
#include <esp_mn_speech_commands.h>
#include <esp_timer.h>
#include <freertos/idf_additions.h>
#include <model_path.h>

extern const uint8_t rodakos_voice_models_start[]
    asm("_binary_rodakos_voice_models_start");

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceAudioFrontend";
constexpr const char* kWakeAudioInputOwner = "voice-wake-frontend";
constexpr const char* kConversationAudioInputOwner = "voice-conversation-frontend";
constexpr int kWakeInputPriority = 10;
constexpr int kConversationInputPriority = 30;
constexpr uint32_t kSampleRate = 16000;
// ES7210 TDM: slot0=MIC1, slot1=MIC3 (ES8311 speaker reference), slot2=MIC2, slot3=MIC4.
// Keep all slots so MIC1/MIC2 can be selected between utterances; MIC3 remains the AEC reference.
constexpr uint16_t kInputChannels = 4;
constexpr uint16_t kMainMicTdmSlot = 2;
constexpr uint16_t kBitsPerSample = 16;
// BigSmart raw TDM order is MIC1, MIC3(reference), MIC2, MIC4.
constexpr uint16_t kInputChannelMask = 0;
constexpr int kInputGain = 30;
constexpr int kDetectionDurationMs = 3000;
// 0.2 is conservative for a quiet near-field lab. The lower threshold is paired
// with a distinctive command and the existing single-command gate so distant
// speech can reach MultiNet without turning arbitrary audio into a wake.
constexpr float kDetectionThreshold = 0.14F;
constexpr size_t kConversationReadSamples = 320;
constexpr size_t kMaxQueuedFrames = 80;
constexpr TickType_t kIdleDelay = pdMS_TO_TICKS(20);
constexpr TickType_t kInputRetryDelay = pdMS_TO_TICKS(100);
constexpr int kIdleCloseIterations = 15;
// 固定 ESP-SR 2.2.2 的 1MIC/WebRTC 合同：feed 返回写入主 FIFO 的字节数。
// WebRTC 按 10ms 分块；fetch 必须有完整帧，短读超时会消费并丢弃 partial PCM。
constexpr size_t kAfeWebRtcBytes = 160 * sizeof(int16_t);
constexpr int kAfeRingFrames = 50;
constexpr size_t kAfeMaxUncertainFrames = 4;
constexpr int64_t kAfeStallUs = 100000;

const char* AfeStageName(AfeProducerStage stage) {
    switch (stage) {
        case AfeProducerStage::kInputOpen: return "input_open";
        case AfeProducerStage::kRawRead: return "raw_read";
        case AfeProducerStage::kReadReturned: return "read_returned";
        case AfeProducerStage::kReadFailed: return "read_failed";
        case AfeProducerStage::kFeedAdmission: return "feed_admission";
        case AfeProducerStage::kPrepareFeed: return "prepare_feed";
        case AfeProducerStage::kFeedAdmitted: return "feed_admitted";
        case AfeProducerStage::kApiBoundary: return "api_boundary";
        case AfeProducerStage::kReturnedWaitPublish: return "returned_wait_publish";
        case AfeProducerStage::kCreditPublished: return "credit_published";
        case AfeProducerStage::kBetweenReads: return "between_reads";
        default: return "unknown";
    }
}

struct AfeGapWindow {
    int64_t began_us = 0;
    uint32_t generation = 0;
    uint32_t epoch = 0;
    unsigned first_stall = 0;
    unsigned last_stall = 0;
    uint32_t max_observation_gap_us = 0;
    uint32_t max_observation_lock_us = 0;
};

void LogAfeProducerObservation(const AfeProducerObservation& producer, uint32_t generation,
                              uint32_t epoch, unsigned stall, unsigned gap_id, int64_t observed_us,
                              const char* event = "stall") {
    const bool same_scope = producer.generation == generation && producer.epoch == epoch;
    ESP_LOGI(TAG, "AFE %s producer: generation=%u epoch=%u count=%u gap_id=%u scope=%s producer_generation=%u producer_epoch=%u stage=%s seq=%u age_us=%u detail=%d previous_us=%u",
             event, static_cast<unsigned>(generation), static_cast<unsigned>(epoch), stall, gap_id,
             same_scope ? "current" : "stale", static_cast<unsigned>(producer.generation),
             static_cast<unsigned>(producer.epoch), AfeStageName(producer.stage),
             static_cast<unsigned>(producer.sequence),
             static_cast<unsigned>(same_scope ? AfeElapsedUs(producer.began_us, observed_us) : 0),
             static_cast<int>(producer.detail), static_cast<unsigned>(producer.previous_elapsed_us));
    ESP_LOGI(TAG, "AFE %s maxima: generation=%u epoch=%u count=%u gap_id=%u scope=%s window=producer_epoch producer_generation=%u producer_epoch=%u read_us=%u read_seq=%u api_wall_us=%u api_seq=%u return_to_publish_us=%u publish_seq=%u",
             event, static_cast<unsigned>(generation), static_cast<unsigned>(epoch), stall, gap_id,
             same_scope ? "current" : "stale", static_cast<unsigned>(producer.generation),
             static_cast<unsigned>(producer.epoch), static_cast<unsigned>(producer.max_read_us),
             static_cast<unsigned>(producer.max_read_sequence), static_cast<unsigned>(producer.max_api_us),
             static_cast<unsigned>(producer.max_api_sequence), static_cast<unsigned>(producer.max_return_to_publish_us),
             static_cast<unsigned>(producer.max_publish_sequence));
}

void LogAfeGapClosed(const AfeGapWindow& gap, const char* reason, int64_t decision_us,
                    int64_t last_fetch_return_us, unsigned fetches, unsigned feed_sequence,
                    unsigned credits, const AfeProducerObservation& producer, int64_t observed_us) {
    ESP_LOGI(TAG, "AFE input gap closed: generation=%u epoch=%u gap_id=%u first_stall=%u last_stall=%u warnings=%u reason=%s elapsed_us=%u decision_elapsed_us=%u fetch_return_age_us=%u observe_gap_max_us=%u observe_lock_max_us=%u fetches=%u feed_seq=%u credits=%u",
             static_cast<unsigned>(gap.generation), static_cast<unsigned>(gap.epoch), gap.first_stall,
             gap.first_stall, gap.last_stall, gap.last_stall - gap.first_stall + 1, reason,
             static_cast<unsigned>(AfeElapsedUs(gap.began_us, observed_us)),
             static_cast<unsigned>(AfeElapsedUs(gap.began_us, decision_us)),
             static_cast<unsigned>(last_fetch_return_us == 0 ? 0 : AfeElapsedUs(last_fetch_return_us, observed_us)),
             static_cast<unsigned>(gap.max_observation_gap_us), static_cast<unsigned>(gap.max_observation_lock_us),
             fetches, feed_sequence, credits);
    LogAfeProducerObservation(producer, gap.generation, gap.epoch, gap.last_stall, gap.first_stall,
                              observed_us, "gap");
}

class LifecycleLock {
public:
    explicit LifecycleLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    }
    ~LifecycleLock() { xSemaphoreGiveRecursive(mutex_); }
private:
    SemaphoreHandle_t mutex_;
};

bool ParseDiagnosticCount(const std::string& text, size_t& value) {
    if (text.empty()) return false;
    value = 0;
    for (char c : text) {
        if (c < '0' || c > '9' || value > 160000) return false;
        value = value * 10 + static_cast<size_t>(c - '0');
    }
    return value <= 160000;
}

int DiagnosticHexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

VoiceAudioFrontend::VoiceAudioFrontend(AudioCodecInput& input) : input_(input) {
    mutex_ = xSemaphoreCreateMutex();
    lifecycle_mutex_ = xSemaphoreCreateRecursiveMutex();
}

VoiceAudioFrontend::~VoiceAudioFrontend() {
    capture_retirement_owner_.Close();
    Deinit();
    capture_retirement_owner_.Drain();
    TaskHandle_t wake_notification_task = nullptr;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        wake_notification_task = wake_notification_task_;
        wake_notification_stopping_ = wake_notification_task != nullptr;
        xSemaphoreGive(mutex_);
    }
    if (wake_notification_task != nullptr) {
        xTaskNotifyGive(wake_notification_task);
        while (eTaskGetState(wake_notification_task) != eSuspended) {
            vTaskDelay(1);
        }
        vTaskDelete(wake_notification_task);
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (wake_notification_task_ == wake_notification_task) {
                wake_notification_task_ = nullptr;
            }
            xSemaphoreGive(mutex_);
        }
    }
    if (lifecycle_mutex_ != nullptr) {
        vSemaphoreDelete(lifecycle_mutex_);
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool VoiceAudioFrontend::Init() {
    if (mutex_ == nullptr || lifecycle_mutex_ == nullptr) {
        return false;
    }
    LifecycleLock lifecycle(lifecycle_mutex_);
    if (deinitializing_ || capture_retirement_owner_.IsClosed()) return false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (initialized_) {
        if (!wake_model_ready_) {
            ReleaseWakeModelLocked();
            if (!InitModelLocked()) {
                xSemaphoreGive(mutex_);
                return false;
            }
        }
        xSemaphoreGive(mutex_);
        return true;
    }

    if (!InitModelLocked()) {
        xSemaphoreGive(mutex_);
        return false;
    }

    initialized_ = true;
    const bool had_wake_notification_task = wake_notification_task_ != nullptr;
    const bool task_started = EnsureWakeNotificationTaskLocked() &&
                              EnsureCaptureTaskLocked();
    TaskHandle_t wake_notification_task_to_delete = nullptr;
    if (!task_started) {
        initialized_ = false;
        ReleaseModelLocked();
        if (!had_wake_notification_task && wake_notification_task_ != nullptr) {
            wake_notification_task_to_delete = wake_notification_task_;
            wake_notification_task_ = nullptr;
        }
    }
    xSemaphoreGive(mutex_);
    if (wake_notification_task_to_delete != nullptr) {
        vTaskDelete(wake_notification_task_to_delete);
    }
    return task_started;
}

void VoiceAudioFrontend::Deinit() {
    if (mutex_ == nullptr || lifecycle_mutex_ == nullptr) {
        return;
    }

    xSemaphoreTakeRecursive(lifecycle_mutex_, portMAX_DELAY);
    deinitializing_ = true;
    Stop();
    ClearDiagnosticAudio();
    StopAecDiagnosticCapture();
    ClearAecDiagnosticCapture();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    task_running_ = false;
    const auto capture_retirement = capture_retirement_ticket_;
    ++wake_generation_;
    mode_ = Mode::kIdle;
    on_wake_word_ = {};
    frames_.clear();
    conversation_assembler_.Reset();
    wake_notification_pending_ = false;
    pending_wake_callback_ = {};
    pending_diagnostic_command_ = {};
    pending_wake_word_.clear();
    pending_wake_generation_ = 0;
    xSemaphoreGive(mutex_);

    input_.CloseForOwner(kWakeAudioInputOwner);
    input_.CloseForOwner(kConversationAudioInputOwner);

    capture_retirement.Join();

    xSemaphoreTake(mutex_, portMAX_DELAY);
    input_open_ = false;
    ReleaseModelLocked();
    initialized_ = false;
    xSemaphoreGive(mutex_);
    xSemaphoreGiveRecursive(lifecycle_mutex_);
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool active = wake_notification_active_;
        xSemaphoreGive(mutex_);
        if (!active || xTaskGetCurrentTaskHandle() == wake_notification_task_) break;
        vTaskDelay(1);
    }
    LifecycleLock lifecycle(lifecycle_mutex_);
    deinitializing_ = false;
}

bool VoiceAudioFrontend::StartListening(
    std::function<void(const std::string&)> on_wake_word) {
    if (lifecycle_mutex_ == nullptr) return false;
    LifecycleLock lifecycle(lifecycle_mutex_);
    if (!Init()) {
        return false;
    }

    Stop();
    ClearDiagnosticAudio();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    ++wake_generation_;
    on_wake_word_ = std::move(on_wake_word);
    mode_ = Mode::kWakeOnly;
    frames_.clear();
    conversation_assembler_.Reset();
    if (multinet_ != nullptr && multinet_data_ != nullptr) {
        multinet_->clean(multinet_data_);
    }
    const bool task_started = EnsureCaptureTaskLocked();
    xSemaphoreGive(mutex_);

    if (!task_started || !EnsureInputForMode(Mode::kWakeOnly)) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (mode_ == Mode::kWakeOnly) {
            mode_ = Mode::kIdle;
            on_wake_word_ = {};
        }
        SetErrorLocked("Microphone unavailable for wake-word monitoring");
        xSemaphoreGive(mutex_);
        input_.CloseForOwner(kWakeAudioInputOwner);
        return false;
    }

    std::string wake_word;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    wake_word = wake_identity_.wake_word;
    xSemaphoreGive(mutex_);
    ESP_LOGI(TAG, "Always-on wake monitoring armed for %s on TDM slot %u (MIC2)",
             wake_word.c_str(), static_cast<unsigned>(kMainMicTdmSlot));
    return true;
}

void VoiceAudioFrontend::StopListening() {
    if (lifecycle_mutex_ == nullptr) return;
    LifecycleLock lifecycle(lifecycle_mutex_);
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    ++wake_generation_;
    if (mode_ == Mode::kWakeOnly) {
        mode_ = Mode::kIdle;
    }
    on_wake_word_ = {};
    xSemaphoreGive(mutex_);
    input_.CloseForOwner(kWakeAudioInputOwner);
}

bool VoiceAudioFrontend::ConfigureWakeWord(const VoiceIdentityConfig& config) {
    VoiceIdentityConfig normalized;
    std::string error;
    if (!NormalizeVoiceIdentityConfig(config, normalized, error)) {
        if (mutex_ == nullptr) return false;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        SetErrorLocked(error.empty() ? "Invalid voice identity" : error.c_str());
        xSemaphoreGive(mutex_);
        return false;
    }
    if (mutex_ == nullptr) return false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (multinet_data_ == nullptr || multinet_ == nullptr) {
        wake_identity_ = normalized;
        wake_model_ready_ = false;
        xSemaphoreGive(mutex_);
        return true;
    }

    const bool updated = esp_mn_commands_clear() == ESP_OK &&
                         esp_mn_commands_add(1, normalized.wake_command.c_str()) == ESP_OK &&
                         esp_mn_commands_update() == nullptr;
    if (!updated) {
        // A failed update may have partially changed the command graph. Do not
        // treat the old model as usable; release it and require an explicit
        // Configure/Init rebuild before listening or detection can resume.
        ReleaseWakeModelLocked();
        SetErrorLocked("MultiNet rejected voice identity command");
        xSemaphoreGive(mutex_);
        return false;
    }
    wake_identity_ = normalized;
    wake_model_ready_ = true;
    xSemaphoreGive(mutex_);
    ESP_LOGI(TAG, "MultiNet wake command updated: display=%s command=%s",
             normalized.wake_word.c_str(), normalized.wake_command.c_str());
    return true;
}

bool VoiceAudioFrontend::IsListening() const {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool listening = initialized_ && task_running_ && input_open_ &&
                           mode_ == Mode::kWakeOnly;
    xSemaphoreGive(mutex_);
    return listening;
}

bool VoiceAudioFrontend::Start(const VoiceRecorderConfig& config) {
    if (lifecycle_mutex_ == nullptr) return false;
    LifecycleLock lifecycle(lifecycle_mutex_);
    if (!Init()) {
        return false;
    }
    if (config.sample_rate != static_cast<int>(kSampleRate) ||
        config.channels != 1 ||
        config.bits_per_sample != static_cast<int>(kBitsPerSample) ||
        config.frame_duration_ms <= 0) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        SetErrorLocked("Unsupported voice recorder format");
        xSemaphoreGive(mutex_);
        return false;
    }

    StopListening();
    Stop();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    recorder_config_ = config;
    const uint32_t generation = ++conversation_generation_;
    const bool task_started = EnsureCaptureTaskLocked();
    xSemaphoreGive(mutex_);
    if (!task_started || !StartAfe(generation)) return false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    mode_ = Mode::kConversation;
    aec_diagnostic_capture_.Begin(generation);
    xSemaphoreGive(mutex_);
    if (!EnsureInputForMode(Mode::kConversation)) {
        Stop();
        return false;
    }

    ESP_LOGI(TAG, "Conversation capture started: %d Hz, %d ms frames",
             config.sample_rate, config.frame_duration_ms);
    return true;
}

void VoiceAudioFrontend::Stop() {
    if (lifecycle_mutex_ == nullptr) return;
    LifecycleLock lifecycle(lifecycle_mutex_);
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (mode_ == Mode::kConversation) {
        mode_ = Mode::kIdle;
    }
    if (aec_diagnostic_capture_.GetStatus().state == VoiceAecDiagnosticCapture::State::kCapturing)
        aec_diagnostic_capture_.Stop();
    ++conversation_generation_;
    frames_.clear();
    conversation_assembler_.Reset();
    xSemaphoreGive(mutex_);
    StopAfe();
    input_.CloseForOwner(kConversationAudioInputOwner);
}

bool VoiceAudioFrontend::IsRunning() const {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool running = initialized_ && task_running_ && mode_ == Mode::kConversation;
    xSemaphoreGive(mutex_);
    return running;
}

bool VoiceAudioFrontend::PopFrame(VoicePcmFrame& frame) {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (frames_.empty()) {
        xSemaphoreGive(mutex_);
        return false;
    }
    frame = std::move(frames_.front());
    frames_.pop_front();
    xSemaphoreGive(mutex_);
    return true;
}

bool VoiceAudioFrontend::ArmAecDiagnosticCapture(uint32_t duration_ms) {
    if (mutex_ == nullptr || lifecycle_mutex_ == nullptr) return false;
    LifecycleLock lifecycle(lifecycle_mutex_);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool accepted = initialized_ && !deinitializing_ && !diagnostic_audio_active_ &&
                          aec_diagnostic_capture_.Arm(duration_ms);
    if (accepted && mode_ == Mode::kConversation)
        aec_diagnostic_capture_.Begin(conversation_generation_);
    xSemaphoreGive(mutex_);
    return accepted;
}

void VoiceAudioFrontend::StopAecDiagnosticCapture() {
    if (mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    aec_diagnostic_capture_.Stop();
    xSemaphoreGive(mutex_);
}

bool VoiceAudioFrontend::ClearAecDiagnosticCapture() {
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool cleared = aec_diagnostic_capture_.Clear();
    xSemaphoreGive(mutex_);
    return cleared;
}

VoiceAecDiagnosticCapture::Status VoiceAudioFrontend::GetAecDiagnosticCaptureStatus() const {
    if (mutex_ == nullptr) return {};
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const auto status = aec_diagnostic_capture_.GetStatus();
    xSemaphoreGive(mutex_);
    return status;
}

bool VoiceAudioFrontend::ReadAecDiagnosticCaptureChunk(uint8_t channel, size_t offset,
                                                      size_t count, std::string& hex) const {
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool read = aec_diagnostic_capture_.ReadChunk(channel, offset, count, hex);
    xSemaphoreGive(mutex_);
    return read;
}

void VoiceAudioFrontend::ClearDiagnosticAudio() {
    if (mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    heap_caps_free(diagnostic_audio_);
    diagnostic_audio_ = nullptr;
    diagnostic_audio_samples_ = 0;
    diagnostic_audio_loaded_ = 0;
    diagnostic_audio_position_ = 0;
    diagnostic_audio_active_ = false;
    xSemaphoreGive(mutex_);
}

bool VoiceAudioFrontend::ArmDiagnosticAudio() {
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const auto capture_state = aec_diagnostic_capture_.GetStatus().state;
    const bool ready = capture_state != VoiceAecDiagnosticCapture::State::kArmed &&
                       capture_state != VoiceAecDiagnosticCapture::State::kCapturing &&
                       mode_ == Mode::kWakeOnly &&
                       diagnostic_audio_samples_ == diagnostic_audio_loaded_;
    if (ready) {
        diagnostic_audio_position_ = 0;
        diagnostic_audio_active_ = diagnostic_audio_ != nullptr;
    }
    xSemaphoreGive(mutex_);
    if (!ready) ESP_LOGW(TAG, "USB simulated wake rejected: not listening or incomplete audio");
    return ready;
}

bool VoiceAudioFrontend::ReplayDiagnosticAudio() {
    if (mutex_ == nullptr || lifecycle_mutex_ == nullptr) return false;
    LifecycleLock lifecycle(lifecycle_mutex_);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const auto capture_state = aec_diagnostic_capture_.GetStatus().state;
    const bool ready = capture_state != VoiceAecDiagnosticCapture::State::kArmed &&
                       capture_state != VoiceAecDiagnosticCapture::State::kCapturing &&
                       initialized_ && !deinitializing_ && mode_ == Mode::kConversation &&
                       afe_data_ != nullptr && !afe_fetch_stopping_ &&
                       afe_generation_ == conversation_generation_ &&
                       diagnostic_audio_ != nullptr && diagnostic_audio_samples_ > 0 &&
                       diagnostic_audio_samples_ == diagnostic_audio_loaded_ &&
                       diagnostic_audio_position_ >= diagnostic_audio_samples_;
    if (ready) {
        diagnostic_audio_position_ = 0;
        diagnostic_audio_active_ = true;
    }
    xSemaphoreGive(mutex_);
    if (ready) ESP_LOGI(TAG, "USB diagnostic audio replay started");
    return ready;
}

bool VoiceAudioFrontend::LoadDiagnosticAudio(const std::string& command) {
    if (mutex_ == nullptr || lifecycle_mutex_ == nullptr) return false;
    LifecycleLock lifecycle(lifecycle_mutex_);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!initialized_ || mode_ != Mode::kWakeOnly || diagnostic_audio_active_ ||
        wake_notification_active_ || wake_notification_pending_ || pending_diagnostic_command_) {
        xSemaphoreGive(mutex_);
        return false;
    }
    bool accepted = false;
    if (command == "audio_clear") {
        heap_caps_free(diagnostic_audio_);
        diagnostic_audio_ = nullptr;
        diagnostic_audio_samples_ = diagnostic_audio_loaded_ = diagnostic_audio_position_ = 0;
        accepted = true;
    } else if (command.rfind("audio_begin ", 0) == 0) {
        size_t count = 0;
        if (ParseDiagnosticCount(command.substr(12), count) && count > 0) {
            auto* replacement = static_cast<int16_t*>(heap_caps_malloc(
                count * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (replacement != nullptr) {
                heap_caps_free(diagnostic_audio_);
                diagnostic_audio_ = replacement;
                diagnostic_audio_samples_ = count;
                diagnostic_audio_loaded_ = diagnostic_audio_position_ = 0;
                accepted = true;
            }
        }
    } else if (command.rfind("audio_chunk ", 0) == 0) {
        const size_t separator = command.find(' ', 12);
        size_t offset = 0;
        if (separator != std::string::npos &&
            ParseDiagnosticCount(command.substr(12, separator - 12), offset)) {
            const size_t hex_length = command.size() - separator - 1;
            const size_t count = hex_length / 4;
            bool valid = diagnostic_audio_ != nullptr && offset == diagnostic_audio_loaded_ &&
                         hex_length > 0 && hex_length <= 1024 && hex_length % 4 == 0 &&
                         count <= diagnostic_audio_samples_ - diagnostic_audio_loaded_;
            for (size_t i = separator + 1; valid && i < command.size(); ++i) {
                valid = DiagnosticHexDigit(command[i]) >= 0;
            }
            if (valid) {
                for (size_t i = 0; i < count; ++i) {
                    const size_t p = separator + 1 + i * 4;
                    const uint16_t sample = static_cast<uint16_t>(
                        (DiagnosticHexDigit(command[p]) << 4) | DiagnosticHexDigit(command[p + 1]) |
                        (DiagnosticHexDigit(command[p + 2]) << 12) | (DiagnosticHexDigit(command[p + 3]) << 8));
                    diagnostic_audio_[offset + i] = static_cast<int16_t>(sample);
                }
                diagnostic_audio_loaded_ += count;
                accepted = true;
            }
        }
    }
    xSemaphoreGive(mutex_);
    return accepted;
}

bool VoiceAudioFrontend::QueueDiagnosticCommand(std::function<void()> command) {
    if (!command || mutex_ == nullptr || lifecycle_mutex_ == nullptr) return false;
    LifecycleLock lifecycle(lifecycle_mutex_);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool accepted = initialized_ && !deinitializing_ &&
                          wake_notification_task_ != nullptr &&
                          !wake_notification_stopping_ && !wake_notification_active_ &&
                          !wake_notification_pending_ && !pending_diagnostic_command_;
    if (accepted) {
        pending_diagnostic_command_ = std::move(command);
        xTaskNotifyGive(wake_notification_task_);
    }
    xSemaphoreGive(mutex_);
    return accepted;
}

void VoiceAudioFrontend::CaptureTaskEntry(void* arg) {
    static_cast<VoiceAudioFrontend*>(arg)->CaptureTask();
}
void VoiceAudioFrontend::AfeFetchTaskEntry(void* arg) { static_cast<VoiceAudioFrontend*>(arg)->AfeFetchTask(); }

void VoiceAudioFrontend::WakeNotificationTaskEntry(void* arg) {
    auto* owner = static_cast<VoiceAudioFrontend*>(arg);
    if (owner == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        std::function<void(const std::string&)> callback;
        std::function<void()> diagnostic_command;
        std::string wake_word;
        bool stopping = false;
        bool should_notify = false;
        if (owner->mutex_ != nullptr) {
            xSemaphoreTake(owner->mutex_, portMAX_DELAY);
            stopping = owner->wake_notification_stopping_;
            if (!stopping) {
                owner->wake_notification_active_ = true;
                diagnostic_command = std::move(owner->pending_diagnostic_command_);
            }
            if (!stopping && owner->wake_notification_pending_) {
                callback = std::move(owner->pending_wake_callback_);
                wake_word = std::move(owner->pending_wake_word_);
                should_notify = owner->initialized_ &&
                                owner->wake_generation_ ==
                                    owner->pending_wake_generation_ &&
                                static_cast<bool>(callback);
                owner->wake_notification_pending_ = false;
                owner->pending_wake_generation_ = 0;
            }
            xSemaphoreGive(owner->mutex_);
        }

        if (stopping) {
            vTaskSuspend(nullptr);
            continue;
        }
        if (should_notify) {
            callback(wake_word);
            ESP_LOGI(TAG,
                     "Wake notification handled: stack_min_free=%u bytes",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) *
                                           sizeof(StackType_t)));
        }
        if (diagnostic_command) {
            diagnostic_command();
            ESP_LOGI(TAG, "USB voice diagnostic handled: stack_min_free=%u bytes",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) *
                                           sizeof(StackType_t)));
        }
        if (owner->mutex_ != nullptr) {
            xSemaphoreTake(owner->mutex_, portMAX_DELAY);
            owner->wake_notification_active_ = false;
            xSemaphoreGive(owner->mutex_);
        }
    }
}

bool VoiceAudioFrontend::InitModelLocked() {
    if (multinet_data_ != nullptr && wake_model_ready_) {
        return true;
    }
    if (multinet_data_ != nullptr) {
        ReleaseWakeModelLocked();
    }

    const bool retain_models = models_ != nullptr;
    if (!retain_models) models_ = srmodel_load(rodakos_voice_models_start);
    const auto fail = [this, retain_models]() {
        if (retain_models) ReleaseWakeModelLocked();
        else ReleaseModelLocked();
        return false;
    };
    if (models_ == nullptr) {
        SetErrorLocked("Embedded speech model unavailable");
        return fail();
    }
    if (models_->num <= 0) {
        SetErrorLocked("Embedded speech model list is empty");
        return fail();
    }

    char* model_name = esp_srmodel_filter(models_, ESP_MN_PREFIX, "cn");
    if (model_name == nullptr) {
        SetErrorLocked("Chinese MultiNet model unavailable");
        return fail();
    }
    // ESP-SR 2.2.2 的 mn5q8_cn create/destroy 拥有全局命令表；新模型需重新核验合同。
    if (std::strcmp(model_name, "mn5q8_cn") != 0) {
        SetErrorLocked("Unsupported MultiNet command ownership contract");
        return fail();
    }

    multinet_ = esp_mn_handle_from_name(model_name);
    if (multinet_ == nullptr) {
        SetErrorLocked("MultiNet runtime unavailable");
        return fail();
    }

    multinet_data_ = multinet_->create(model_name, kDetectionDurationMs);
    if (multinet_data_ == nullptr) {
        SetErrorLocked("MultiNet initialization failed");
        return fail();
    }

    multinet_->set_det_threshold(multinet_data_, kDetectionThreshold);
    // create 已创建命令表；此处只登记命令，失败时也统一由 destroy 释放。
    if (esp_mn_commands_add(1, wake_identity_.wake_command.c_str()) != ESP_OK) {
        SetErrorLocked("Wake command registration failed");
        return fail();
    }
    if (esp_mn_commands_update() != nullptr) {
        SetErrorLocked("Wake command is unsupported by the speech model");
        return fail();
    }
    wake_chunk_samples_ = static_cast<size_t>(multinet_->get_samp_chunksize(multinet_data_));
    if (wake_chunk_samples_ == 0) {
        SetErrorLocked("Invalid MultiNet audio chunk size");
        return fail();
    }

    wake_model_ready_ = true;
    return true;
}

bool VoiceAudioFrontend::StartAfe(uint32_t generation) {
#if defined(RODAKOS_RELEASE_TESTS)
    InitializeVoiceTickObserver();
    VoiceTickBeginSnapshot tick_begin{};
#endif
    // Lifecycle transitions are serialized; workers only see a published instance.
    xSemaphoreTake(mutex_, portMAX_DELAY);
    afe_config_t* afe_config = afe_config_init("MR", nullptr, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    if (afe_config == nullptr) { SetErrorLocked("AFE configuration failed"); xSemaphoreGive(mutex_); return false; }
    afe_config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
    afe_config->vad_mode = VAD_MODE_0;
    afe_config->vad_model_name = nullptr;
    afe_config->vad_min_speech_ms = 128;
    afe_config->vad_min_noise_ms = 200;
    afe_config->vad_delay_ms = 128;
    afe_config->vad_mute_playback = false;
    afe_config->vad_init = true;
#if CONFIG_USE_DEVICE_AEC
    afe_config->aec_init = true;
#else
    afe_config->aec_init = false;
#endif
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config->afe_ringbuf_size = kAfeRingFrames;
    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_ ? afe_iface_->create_from_config(afe_config) : nullptr;
    afe_config_free(afe_config);
    if (afe_data_ == nullptr) { afe_iface_ = nullptr; SetErrorLocked("AFE initialization failed"); xSemaphoreGive(mutex_); return false; }
    const int feed_samples = afe_iface_->get_feed_chunksize(afe_data_);
    const int fetch_samples = afe_iface_->get_fetch_chunksize(afe_data_);
#if CONFIG_USE_DEVICE_AEC
    constexpr int expected_feed_samples = 256;
#else
    constexpr int expected_feed_samples = 160;
#endif
    if (feed_samples != expected_feed_samples || fetch_samples != 512 ||
        afe_iface_->reset_buffer == nullptr || afe_iface_->reset_vad == nullptr ||
        afe_iface_->get_feed_channel_num(afe_data_) != 2 ||
        afe_iface_->get_fetch_channel_num(afe_data_) != 1 ||
        afe_iface_->get_samp_rate(afe_data_) != kSampleRate) {
        afe_iface_->destroy(afe_data_);
        afe_data_ = nullptr;
        afe_iface_ = nullptr;
        SetErrorLocked("Invalid AFE MR input format");
        xSemaphoreGive(mutex_);
        return false;
    }
    afe_generation_ = generation;
    afe_fetch_stopping_ = false;
    afe_feed_started_ = false;
    afe_fetch_bytes_ = static_cast<size_t>(fetch_samples) * sizeof(int16_t);
    afe_feed_max_bytes_ = ((static_cast<size_t>(feed_samples) + 159) / 160) * kAfeWebRtcBytes;
    afe_capacity_bytes_ = afe_fetch_bytes_ * kAfeRingFrames;
    afe_credit_bytes_ = afe_uncertain_bytes_ = 0;
    afe_resync_pending_ = false;
    afe_stream_epoch_ = 0;
    afe_feed_calls_ = afe_feed_returns_ = afe_feed_errors_ = 0;
    afe_started_us_ = esp_timer_get_time();
#if defined(RODAKOS_RELEASE_TESTS)
    BeginVoiceTickObservation(generation, 0, afe_started_us_, &tick_begin);
#endif
    ESP_LOGI(TAG, "AFE flow ready: generation=%u feed_samples=%d fetch_bytes=%u capacity=%u",
             static_cast<unsigned>(generation), feed_samples,
             static_cast<unsigned>(afe_fetch_bytes_), static_cast<unsigned>(afe_capacity_bytes_));
    if (xTaskCreatePinnedToCoreWithCaps(AfeFetchTaskEntry, "afe_fetch", 6144, this, 4,
                                &afe_fetch_task_, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        afe_iface_->destroy(afe_data_); afe_data_ = nullptr; afe_iface_ = nullptr;
        SetErrorLocked("AFE fetch task creation failed"); xSemaphoreGive(mutex_);
#if defined(RODAKOS_RELEASE_TESTS)
        LogVoiceTickBegin(generation, 0, tick_begin);
        ObserveVoiceTickBoundary("fetch_create_failed", generation, 0, tick_begin.token, 0,
                                 esp_timer_get_time(), true);
#endif
        return false;
    }
    xSemaphoreGive(mutex_);
#if defined(RODAKOS_RELEASE_TESTS)
    LogVoiceTickBegin(generation, 0, tick_begin);
#endif
    return true;
}

void VoiceAudioFrontend::ReleaseWakeModelLocked() {
    if (multinet_data_ != nullptr && multinet_ != nullptr) {
        multinet_->destroy(multinet_data_);
    }
    multinet_data_ = nullptr;
    multinet_ = nullptr;
    wake_chunk_samples_ = 0;
    wake_model_ready_ = false;
}

void VoiceAudioFrontend::ReleaseModelLocked() {
    ReleaseWakeModelLocked();
    if (models_ != nullptr) {
        esp_srmodel_deinit(models_);
        models_ = nullptr;
    }
}

bool VoiceAudioFrontend::EnsureCaptureTaskLocked() {
    if (task_ != nullptr) {
        task_running_ = true;
        return true;
    }

    const auto previous_retirement = capture_retirement_ticket_;
    auto retirement = ReserveTaskRetirement(
        capture_retirement_owner_, CaptureTaskEntry, this);
    if (!retirement) {
        SetErrorLocked("Voice capture task retirement unavailable");
        return false;
    }
    capture_retirement_ticket_ = retirement;
    task_running_ = true;
    TaskHandle_t created_task = nullptr;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    const BaseType_t created = xTaskCreatePinnedToCoreWithCaps(
        TaskRetirementEntry, "voice_frontend", 8192, TaskRetirementContext(retirement),
        4, &created_task, 0,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    const BaseType_t created = xTaskCreateWithCaps(
        TaskRetirementEntry, "voice_frontend", 8192, TaskRetirementContext(retirement),
        4, &created_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (created != pdPASS) {
        CancelTaskRetirement(retirement);
        capture_retirement_ticket_ = previous_retirement;
        task_running_ = false;
        task_ = nullptr;
        SetErrorLocked("Voice capture task creation failed");
        return false;
    }
    task_ = created_task;
    PublishTaskRetirement(retirement, created_task);
    return true;
}

bool VoiceAudioFrontend::EnsureWakeNotificationTaskLocked() {
    if (wake_notification_task_ != nullptr) {
        return true;
    }

    wake_notification_stopping_ = false;
    // 回调会读取 NVS；SPI Flash 临时关闭外部 RAM cache 时，任务栈必须位于内部 SRAM。
    const BaseType_t created = xTaskCreate(
        WakeNotificationTaskEntry, "wake_notify", 6144, this, 4,
        &wake_notification_task_);
    if (created != pdPASS) {
        wake_notification_task_ = nullptr;
        SetErrorLocked("Wake notification task unavailable");
        return false;
    }
    ESP_LOGI(TAG,
             "Wake notification task ready in internal SRAM: free=%u largest=%u",
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    return true;
}

bool VoiceAudioFrontend::EnsureInputForMode(Mode mode) {
    if (mode == Mode::kIdle) {
        return true;
    }
    const int priority = mode == Mode::kConversation
                             ? kConversationInputPriority
                             : kWakeInputPriority;
    const char* owner = mode == Mode::kConversation
                            ? kConversationAudioInputOwner
                            : kWakeAudioInputOwner;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (mode_ != mode) {
        xSemaphoreGive(mutex_);
        return false;
    }
    const bool opened = input_.OpenForOwner(owner,
                                             priority,
                                             kSampleRate,
                                             kInputChannels,
                                             kBitsPerSample,
                                             kInputGain,
                                             kInputChannelMask,
                                             mode == Mode::kConversation
                                                 ? AudioCodecInput::InputGainProfile::kAecReference10Db
                                                 : AudioCodecInput::InputGainProfile::kUniform);
    input_open_ = opened;
    if (opened) {
        last_error_.clear();
    } else {
        last_error_ = mode == Mode::kWakeOnly
                          ? "Microphone unavailable for wake-word monitoring"
                          : "Microphone unavailable for assistant session";
    }
    xSemaphoreGive(mutex_);
    return opened;
}

void VoiceAudioFrontend::CaptureTask() {
    int idle_iterations = 0;
    std::vector<int16_t> afe_feed_buffer;
    uint32_t feed_generation = 0;
    uint32_t feed_epoch = 0;
    uint32_t read_sequence = 0;
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool task_running = task_running_;
        const Mode mode = mode_;
        const uint32_t generation = conversation_generation_;
        const uint32_t read_epoch = afe_stream_epoch_;
        const uint32_t wake_generation = wake_generation_;
        const size_t read_samples = ResolveReadSamples(mode) * kInputChannels;
        xSemaphoreGive(mutex_);

        if (!task_running) {
            break;
        }
        if (mode == Mode::kIdle || read_samples == 0) {
            afe_feed_buffer.clear();
            ++idle_iterations;
            if (idle_iterations >= kIdleCloseIterations) {
                xSemaphoreTake(mutex_, portMAX_DELAY);
                if (mode_ == Mode::kIdle) {
                    input_.CloseForOwner(kWakeAudioInputOwner);
                    input_.CloseForOwner(kConversationAudioInputOwner);
                    input_open_ = false;
                }
                xSemaphoreGive(mutex_);
                idle_iterations = 0;
            }
            vTaskDelay(kIdleDelay);
            continue;
        }

        idle_iterations = 0;
        if (mode == Mode::kConversation)
            afe_producer_diagnostics_.Publish(AfeProducerStage::kInputOpen, generation, read_epoch,
                                              read_sequence, esp_timer_get_time());
        if (!EnsureInputForMode(mode)) {
            vTaskDelay(kInputRetryDelay);
            continue;
        }

        std::vector<int16_t> samples(read_samples);
        const char* owner = mode == Mode::kConversation
                                ? kConversationAudioInputOwner
                                : kWakeAudioInputOwner;
        const int64_t read_begin_us = mode == Mode::kConversation ? esp_timer_get_time() : 0;
        if (mode == Mode::kConversation)
            afe_producer_diagnostics_.Publish(AfeProducerStage::kRawRead, generation, read_epoch,
                                              ++read_sequence, read_begin_us);
        const bool read_ok = input_.ReadForOwner(owner, samples.data(),
                                                static_cast<int>(samples.size() * sizeof(int16_t)));
        const int64_t raw_observed_us = esp_timer_get_time();
        if (mode == Mode::kConversation)
            afe_producer_diagnostics_.Publish(read_ok ? AfeProducerStage::kReadReturned
                                                       : AfeProducerStage::kReadFailed,
                generation, read_epoch, read_sequence, raw_observed_us,
                read_ok ? static_cast<int32_t>(samples.size() / kInputChannels) : 0,
                AfeElapsedUs(read_begin_us, raw_observed_us));
        if (!read_ok) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (mode == Mode::kConversation)
                aec_diagnostic_capture_.MarkDiscontinuity(true, generation);
            input_open_ = false;
            if (mode_ == mode) {
                last_error_ = mode == Mode::kWakeOnly
                                  ? "Microphone read failed during wake-word monitoring"
                                  : "Microphone read failed during assistant session";
            }
            xSemaphoreGive(mutex_);
            vTaskDelay(kInputRetryDelay);
            continue;
        }

        std::vector<int16_t> selected_samples;
        SelectMainMicrophone(samples, selected_samples);
        if (mode == Mode::kConversation) {
            afe_producer_diagnostics_.Publish(AfeProducerStage::kFeedAdmission, generation, read_epoch,
                                              read_sequence, esp_timer_get_time());
            xSemaphoreTake(mutex_, portMAX_DELAY);
            const bool can_feed = mode_ == mode && generation == conversation_generation_ &&
                                  afe_data_ != nullptr && !afe_fetch_stopping_ && !afe_resync_pending_ &&
                                  read_epoch == afe_stream_epoch_;
            const uint32_t stream_epoch = afe_stream_epoch_;
            if (can_feed) {
                afe_feed_active_ = true;
                aec_diagnostic_capture_.AppendRaw(samples.data(), samples.size() / kInputChannels,
                    generation, raw_observed_us, selected_main_mic_ == 1 ? 0 : 2);
                if (diagnostic_audio_active_) {
                    for (auto& sample : selected_samples) {
                        sample = diagnostic_audio_position_ < diagnostic_audio_samples_
                                     ? diagnostic_audio_[diagnostic_audio_position_++] : 0;
                    }
                } else {
                    for (size_t i = 0; i < selected_samples.size(); ++i) {
                        selected_samples[i] = samples[i * 4 + (selected_main_mic_ == 1 ? 0 : 2)];
                    }
                }
            } else if (mode_ == mode && generation == conversation_generation_ &&
                       afe_data_ != nullptr && (afe_resync_pending_ || read_epoch != afe_stream_epoch_)) {
                // 此 raw read 已消耗但未 AppendRaw；取消/旧代不计入当前 capture。
                aec_diagnostic_capture_.MarkDiscontinuity(true, generation);
            }
            xSemaphoreGive(mutex_);
            if (can_feed) {
                afe_producer_diagnostics_.Publish(AfeProducerStage::kPrepareFeed, generation, stream_epoch,
                    read_sequence, esp_timer_get_time(), static_cast<int32_t>(afe_feed_buffer.size() / 2));
                if (feed_generation != generation || feed_epoch != stream_epoch) {
                    afe_feed_buffer.clear();
                    feed_generation = generation;
                    feed_epoch = stream_epoch;
                }
                for (size_t i = 0; i < samples.size() / kInputChannels; ++i) {
                    afe_feed_buffer.push_back(selected_samples[i]);
                    afe_feed_buffer.push_back(samples[i * 4 + 1]);
                }
                const size_t feed_size = static_cast<size_t>(afe_iface_->get_feed_chunksize(afe_data_)) * 2;
                while (feed_size > 0 && afe_feed_buffer.size() >= feed_size) {
                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    if (afe_resync_pending_) {
                        // 重同步会丢弃旧输入尾部，避免把 reset 前后数据重新拼成连续输入。
                        afe_feed_buffer.clear();
                        xSemaphoreGive(mutex_);
                        break;
                    }
                    afe_feed_started_ = true;
                    const unsigned feed_call = ++afe_feed_calls_;
                    const int64_t feed_started_us = esp_timer_get_time();
                    const int64_t afe_started_us = afe_started_us_;
                    afe_producer_diagnostics_.Publish(AfeProducerStage::kFeedAdmitted, generation, stream_epoch,
                        feed_call, feed_started_us, static_cast<int32_t>(afe_feed_buffer.size() / 2));
                    xSemaphoreGive(mutex_);
                    // The marker-to-return interval includes scheduling and SDK waits, not DSP CPU time.
                    const int64_t api_begin_us = esp_timer_get_time();
                    afe_producer_diagnostics_.Publish(AfeProducerStage::kApiBoundary, generation, stream_epoch,
                                                      feed_call, api_begin_us);
#if defined(RODAKOS_RELEASE_TESTS)
                    const auto feed_progress_ticket = ArmVoiceFeedProgress(
                        generation, stream_epoch, feed_call,
                        reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()),
                        api_begin_us, afe_started_us);
#endif
                    const int written = afe_iface_->feed(afe_data_, afe_feed_buffer.data());
                    const int64_t api_return_us = esp_timer_get_time();
#if defined(RODAKOS_RELEASE_TESTS)
                    VoiceFeedProgressSnapshot feed_progress;
                    CloseVoiceFeedProgress(feed_progress_ticket, api_return_us, feed_progress);
#endif
                    afe_producer_diagnostics_.Publish(AfeProducerStage::kReturnedWaitPublish, generation,
                        stream_epoch, feed_call, api_return_us, written, AfeElapsedUs(api_begin_us, api_return_us));
                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    ++afe_feed_returns_;
                    const bool current = mode_ == Mode::kConversation &&
                                         generation == conversation_generation_;
                    const bool valid = written > 0 &&
                        static_cast<size_t>(written) <= afe_feed_max_bytes_ &&
                        static_cast<size_t>(written) % kAfeWebRtcBytes == 0;
                    const bool overflow = valid && (afe_credit_bytes_ > afe_capacity_bytes_ ||
                        static_cast<size_t>(written) > afe_capacity_bytes_ - afe_credit_bytes_);
                    if (valid && !overflow) {
                        afe_credit_bytes_ += static_cast<size_t>(written);
                    } else if (current) {
                        ++afe_feed_errors_;
                        InvalidateAfeContinuityLocked();
                        // 0 是本次输出被丢弃；其他异常可能已写部分数据，须在无 feed 时重同步。
                        if (written != 0 || afe_uncertain_bytes_ != 0) afe_resync_pending_ = true;
                    }
                    const int64_t published_us = esp_timer_get_time();
                    afe_producer_diagnostics_.Publish(AfeProducerStage::kCreditPublished, generation,
                        stream_epoch, feed_call, published_us, written, AfeElapsedUs(api_return_us, published_us));
                    const unsigned feed_errors = afe_feed_errors_;
                    const unsigned credits = static_cast<unsigned>(afe_credit_bytes_);
                    const bool resync = afe_resync_pending_;
                    const bool report_error = current && (!valid || overflow) &&
                                              (feed_errors == 1 || feed_errors % 100 == 0);
                    xSemaphoreGive(mutex_);
#if defined(RODAKOS_RELEASE_TESTS)
                    LogVoiceFeedProgress("complete", feed_progress, published_us);
#endif
                    if (report_error)
                        ESP_LOGW(TAG, "AFE feed rejected: count=%u returned=%d credits=%u resync=%d",
                                 feed_errors, written, credits, resync);
                    if (feed_call == 1) {
                        ESP_LOGI(TAG, "AFE first feed: generation=%u begin_ms=%lld before_api_us=%u api_wall_us=%u return_to_publish_us=%u written=%d",
                                 static_cast<unsigned>(generation),
                                 static_cast<long long>((feed_started_us - afe_started_us) / 1000),
                                 static_cast<unsigned>(AfeElapsedUs(feed_started_us, api_begin_us)),
                                 static_cast<unsigned>(AfeElapsedUs(api_begin_us, api_return_us)),
                                 static_cast<unsigned>(AfeElapsedUs(api_return_us, published_us)), written);
                    }
                    afe_feed_buffer.erase(afe_feed_buffer.begin(), afe_feed_buffer.begin() + feed_size);
                }
                xSemaphoreTake(mutex_, portMAX_DELAY);
                afe_feed_active_ = false;
                xSemaphoreGive(mutex_);
            }
            afe_producer_diagnostics_.Publish(AfeProducerStage::kBetweenReads, generation, read_epoch,
                read_sequence, esp_timer_get_time(), static_cast<int32_t>(afe_feed_buffer.size() / 2));
        } else if (mode == Mode::kWakeOnly) {
            ProcessWakeSamples(selected_samples, wake_generation);
        }
        // Keep IDLE0 watchdog serviceable when the codec returns short/empty blocks.
        vTaskDelay(1);
    }

    input_.CloseForOwner(kWakeAudioInputOwner);
    input_.CloseForOwner(kConversationAudioInputOwner);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    input_open_ = false;
    task_ = nullptr;
    xSemaphoreGive(mutex_);
}

void VoiceAudioFrontend::StopAfe() {
    // mode is already idle: no new feed leases. Keep fetch draining until the
    // last feed returns, without holding the mutex needed by the consumer.
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool feeding = afe_feed_active_;
        if (!feeding) afe_fetch_stopping_ = true;
        xSemaphoreGive(mutex_);
        if (!feeding) break;
        vTaskDelay(1);
    }
    TaskHandle_t worker = afe_fetch_task_;
    if (worker != nullptr) {
        while (eTaskGetState(worker) != eSuspended) vTaskDelay(1);
        vTaskDeleteWithCaps(worker);
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    afe_fetch_task_ = nullptr;
    if (afe_data_ != nullptr) afe_iface_->destroy(afe_data_);
    afe_data_ = nullptr;
    afe_iface_ = nullptr;
    xSemaphoreGive(mutex_);
}

void VoiceAudioFrontend::AfeFetchTask() {
#if defined(RODAKOS_RELEASE_TESTS)
    uint32_t tick_token = 0;
    bool tick_initialized = false;
    uint32_t tick_epoch = 0;
#endif
    unsigned fetch_errors = 0;
    unsigned cancelled_results = 0;
    unsigned stalls = 0;
    unsigned resyncs = 0;
    unsigned resync_errors = 0;
    unsigned resync_discarded = 0;
    unsigned fetch_calls = 0;
    bool output_started = false;
    bool fetch_started = false;
    bool stall_reported = false;
    int64_t waiting_since_us = 0;
    int64_t last_observation_us = 0;
    int64_t last_fetch_return_us = 0;
    int64_t last_successful_fetch_us = 0;
    uint32_t waiting_max_observation_gap_us = 0;
    uint32_t waiting_max_observation_lock_us = 0;
    AfeGapWindow gap;
    uint32_t generation = 0;
    while (true) {
        const int64_t lock_begin_us = esp_timer_get_time();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const int64_t acquired_us = esp_timer_get_time();
        const uint32_t observation_gap_us = last_observation_us == 0 ? 0 :
            AfeElapsedUs(last_observation_us, acquired_us);
        const uint32_t observation_lock_us = AfeElapsedUs(lock_begin_us, acquired_us);
        last_observation_us = acquired_us;
        if (gap.began_us != 0) {
            gap.max_observation_gap_us = std::max(gap.max_observation_gap_us, observation_gap_us);
            gap.max_observation_lock_us = std::max(gap.max_observation_lock_us, observation_lock_us);
        }
        const bool stopping = afe_fetch_stopping_;
        generation = afe_generation_;
        const uint32_t epoch = afe_stream_epoch_;
#if defined(RODAKOS_RELEASE_TESTS)
        if (!tick_initialized) {
            tick_token = CurrentVoiceTickObservation(generation, epoch);
            tick_initialized = true;
        }
        tick_epoch = epoch;
#endif
        const bool active = mode_ == Mode::kConversation && generation == conversation_generation_;
        if (!stopping && active && afe_resync_pending_ && !afe_feed_active_) {
#if defined(RODAKOS_RELEASE_TESTS)
            const uint32_t resync_gap_id = gap.began_us != 0 ? gap.first_stall : 0;
#endif
            // Only this worker reads; admission is stopped and existing leases have returned.
            // Reset retains the SDK's WebRTC tail, so output continues using actual byte credits.
            const int reset = afe_iface_->reset_buffer(afe_data_);
            const int reset_vad = afe_iface_->reset_vad(afe_data_);
            const bool reset_ok = reset == 1 && reset_vad == 1;
            if (reset_ok) {
                afe_credit_bytes_ = afe_uncertain_bytes_ = 0;
                afe_resync_pending_ = false;
                ++afe_stream_epoch_;
                ++resyncs;
                resync_errors = 0;
                InvalidateAfeContinuityLocked();
            } else {
                ++resync_errors;
                if (resync_errors >= 3) {
                    SetErrorLocked("AFE buffer recovery failed");
                    mode_ = Mode::kIdle;
                    afe_fetch_stopping_ = true;
                }
            }
            waiting_since_us = 0;
            stall_reported = false;
            const unsigned feed_sequence = afe_feed_calls_;
            const unsigned credits = static_cast<unsigned>(afe_credit_bytes_);
            const int64_t reset_observed_us = esp_timer_get_time();
#if defined(RODAKOS_RELEASE_TESTS)
            VoiceTickSnapshot tick_resync_snapshot;
#endif
            if (gap.began_us != 0 && (reset_ok || resync_errors >= 3)) {
                const auto producer = afe_producer_diagnostics_.Snapshot();
                const int64_t closed_us = esp_timer_get_time();
                xSemaphoreGive(mutex_);
#if defined(RODAKOS_RELEASE_TESTS)
                tick_token = FreezeVoiceTickObservation(generation, epoch, tick_token,
                    resync_gap_id, closed_us, reset_ok || resync_errors >= 3, tick_resync_snapshot);
#endif
                LogAfeGapClosed(gap, reset_ok ? "resynced" : "cancelled", reset_observed_us,
                                last_fetch_return_us, fetch_calls, feed_sequence, credits,
                                producer, closed_us);
                gap = {};
            } else {
                xSemaphoreGive(mutex_);
#if defined(RODAKOS_RELEASE_TESTS)
                tick_token = FreezeVoiceTickObservation(generation, epoch, tick_token,
                    resync_gap_id, reset_observed_us, reset_ok || resync_errors >= 3, tick_resync_snapshot);
#endif
            }
#if defined(RODAKOS_RELEASE_TESTS)
            LogVoiceTickObservation("resync", tick_resync_snapshot);
            if (reset_ok) {
                tick_token = BeginVoiceTickObservation(generation, epoch + 1, reset_observed_us);
                tick_epoch = epoch + 1;
            }
#endif
            if (reset_ok)
                ESP_LOGW(TAG, "AFE flow resynchronized: generation=%u count=%u",
                         static_cast<unsigned>(generation), resyncs);
            else
                ESP_LOGW(TAG, "AFE flow reset failed: generation=%u count=%u status=%d vad_status=%d",
                         static_cast<unsigned>(generation), resync_errors, reset, reset_vad);
            vTaskDelay(reset_ok ? 1 : pdMS_TO_TICKS(100));
            continue;
        }
        const bool resync_draining = active && afe_resync_pending_ && afe_feed_active_;
        const bool ready = afe_credit_bytes_ >= afe_fetch_bytes_;
        // Cancellation/resync must keep the consumer available to outstanding producers.
        const bool fed = afe_feed_started_ && (ready || !active || resync_draining);
        if (!stopping && active && !fed) {
            const int64_t now = esp_timer_get_time();
            if (waiting_since_us == 0) {
                waiting_since_us = now;
                waiting_max_observation_gap_us = observation_gap_us;
                waiting_max_observation_lock_us = observation_lock_us;
            } else {
                waiting_max_observation_gap_us = std::max(waiting_max_observation_gap_us, observation_gap_us);
                waiting_max_observation_lock_us = std::max(waiting_max_observation_lock_us, observation_lock_us);
            }
            if (!stall_reported && now - waiting_since_us >= kAfeStallUs) {
                stall_reported = true;
                ++stalls;
                InvalidateAfeContinuityLocked();
                if (gap.began_us == 0)
                    gap = {waiting_since_us, generation, epoch, stalls, stalls,
                           waiting_max_observation_gap_us, waiting_max_observation_lock_us};
                gap.last_stall = stalls;
                const auto producer = afe_producer_diagnostics_.Snapshot();
                // Sample after the independent tuple copy: its stage timestamp cannot be future.
                const int64_t observed_us = esp_timer_get_time();
                const unsigned feeds = afe_feed_calls_;
                const unsigned returns = afe_feed_returns_;
                const unsigned credits = static_cast<unsigned>(afe_credit_bytes_);
                const unsigned uncertain = static_cast<unsigned>(afe_uncertain_bytes_);
                const bool feed_active = afe_feed_active_;
                const bool resync = afe_resync_pending_;
                xSemaphoreGive(mutex_);
#if defined(RODAKOS_RELEASE_TESTS)
                VoiceTickSnapshot tick_snapshot;
                tick_token = FreezeVoiceTickObservation(generation, epoch, tick_token,
                    gap.first_stall, observed_us, false, tick_snapshot);
                VoiceFeedProgressSnapshot feed_progress;
                if (producer.stage == AfeProducerStage::kApiBoundary &&
                    producer.generation == generation && producer.epoch == epoch) {
                    SnapshotOpenVoiceFeedProgress(generation, epoch, producer.sequence, feed_progress);
                } else {
                    feed_progress.identity.generation = generation;
                    feed_progress.identity.epoch = epoch;
                    feed_progress.identity.sequence = producer.sequence;
                    feed_progress.status = kVoiceFeedProgressStale;
                    feed_progress.flags = kVoiceFeedProgressProducerUnaligned | kVoiceFeedProgressArmAfterUnknown;
                }
                // Logging can block while Capture advances the live feed identity.
                LogVoiceTickObservation("stall", tick_snapshot);
                LogVoiceFeedProgress("open", feed_progress);
#endif
                ESP_LOGW(TAG, "AFE input stalled: phase=%s generation=%u count=%u feeds=%u returns=%u credits=%u epoch=%u gap_id=%u uncertain=%u feed_active=%d resync=%d wait_us=%u observe_gap_us=%u observe_lock_us=%u observe_gap_max_us=%u observe_lock_max_us=%u fetch_return_age_us=%u output_age_us=%u",
                         output_started ? "running" : "warmup", static_cast<unsigned>(generation),
                         stalls, feeds, returns, credits, static_cast<unsigned>(epoch), gap.first_stall,
                         uncertain, feed_active, resync,
                         static_cast<unsigned>(AfeElapsedUs(waiting_since_us, observed_us)),
                         static_cast<unsigned>(observation_gap_us), static_cast<unsigned>(observation_lock_us),
                         static_cast<unsigned>(gap.max_observation_gap_us), static_cast<unsigned>(gap.max_observation_lock_us),
                         static_cast<unsigned>(last_fetch_return_us == 0 ? 0 : AfeElapsedUs(last_fetch_return_us, observed_us)),
                         static_cast<unsigned>(last_successful_fetch_us == 0 ? 0 : AfeElapsedUs(last_successful_fetch_us, observed_us)));
                LogAfeProducerObservation(producer, generation, epoch, stalls, gap.first_stall, observed_us);
                vTaskDelay(1);
                continue;
            }
        }
        bool first_fetch = false;
        if (!stopping && fed) {
            // A failed fetch consumes at most the prepaid frame; remaining credit stays conservative.
            if (ready) afe_credit_bytes_ -= afe_fetch_bytes_;
            waiting_since_us = 0;
            stall_reported = false;
            first_fetch = !fetch_started;
            fetch_started = true;
        }
        const unsigned feed_sequence = afe_feed_calls_;
        const unsigned feed_returns = afe_feed_returns_;
        const unsigned credits = static_cast<unsigned>(afe_credit_bytes_);
        const int64_t event_us = esp_timer_get_time();
        const int64_t started_us = afe_started_us_;
        if (gap.began_us != 0 && (!active || stopping)) {
            const auto producer = afe_producer_diagnostics_.Snapshot();
            const int64_t closed_us = esp_timer_get_time();
            xSemaphoreGive(mutex_);
#if defined(RODAKOS_RELEASE_TESTS)
            VoiceTickSnapshot tick_snapshot;
            tick_token = FreezeVoiceTickObservation(generation, epoch, tick_token,
                gap.first_stall, closed_us, false, tick_snapshot);
#endif
            LogAfeGapClosed(gap, "cancelled", event_us, last_fetch_return_us, fetch_calls,
                            feed_sequence, credits, producer, closed_us);
#if defined(RODAKOS_RELEASE_TESTS)
            LogVoiceTickObservation("cancelled", tick_snapshot);
#endif
            gap = {};
        } else {
            xSemaphoreGive(mutex_);
        }
        if (first_fetch)
            ESP_LOGI(TAG, "AFE first fetch: generation=%u elapsed_ms=%lld feeds=%u returns=%u credits=%u",
                     static_cast<unsigned>(generation), static_cast<long long>((event_us - started_us) / 1000),
                     feed_sequence, feed_returns, credits);
        if (stopping) break;
        if (!fed) {
            vTaskDelay(1);
            continue;
        }
        ++fetch_calls;
        auto* result = afe_iface_->fetch_with_delay(afe_data_, pdMS_TO_TICKS(100));
        const int64_t fetch_return_us = esp_timer_get_time();
        last_fetch_return_us = fetch_return_us;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const int64_t result_observed_us = esp_timer_get_time();
        if (gap.began_us != 0) {
            gap.max_observation_gap_us = std::max(gap.max_observation_gap_us,
                                                 AfeElapsedUs(last_observation_us, result_observed_us));
            gap.max_observation_lock_us = std::max(gap.max_observation_lock_us,
                                                  AfeElapsedUs(fetch_return_us, result_observed_us));
        }
        last_observation_us = result_observed_us;
        const bool stopped = afe_fetch_stopping_;
        const bool current = !stopped && mode_ == Mode::kConversation &&
                             generation == conversation_generation_ &&
                             generation == afe_generation_;
        if (!current) {
            ++cancelled_results;
            const unsigned cancelled_feed_sequence = afe_feed_calls_;
            const unsigned cancelled_credits = static_cast<unsigned>(afe_credit_bytes_);
            if (gap.began_us != 0) {
                const auto producer = afe_producer_diagnostics_.Snapshot();
                const int64_t closed_us = esp_timer_get_time();
                xSemaphoreGive(mutex_);
#if defined(RODAKOS_RELEASE_TESTS)
                VoiceTickSnapshot tick_snapshot;
                tick_token = FreezeVoiceTickObservation(generation, epoch, tick_token,
                    gap.first_stall, closed_us, false, tick_snapshot);
#endif
                LogAfeGapClosed(gap, "cancelled", result_observed_us, last_fetch_return_us, fetch_calls,
                                cancelled_feed_sequence, cancelled_credits, producer, closed_us);
#if defined(RODAKOS_RELEASE_TESTS)
                LogVoiceTickObservation("cancelled", tick_snapshot);
#endif
                gap = {};
            } else {
                xSemaphoreGive(mutex_);
            }
            // Stop closes admission first; existing feed may still need this reader to drain.
            if (stopped) break;
            vTaskDelay(1);
            continue;
        }
        if (resync_draining || afe_resync_pending_) {
            ++resync_discarded;
            xSemaphoreGive(mutex_);
            vTaskDelay(1);
            continue;
        }
        const bool invalid = result == nullptr || result->ret_value != ESP_OK ||
                             result->data == nullptr || result->data_size <= 0 ||
                             static_cast<size_t>(result->data_size) != afe_fetch_bytes_;
        const bool first_output = !invalid && !output_started;
        if (invalid) {
            InvalidateAfeContinuityLocked();
            afe_uncertain_bytes_ = std::min(afe_capacity_bytes_, afe_uncertain_bytes_ + afe_fetch_bytes_);
            if (afe_uncertain_bytes_ >= kAfeMaxUncertainFrames * afe_fetch_bytes_)
                afe_resync_pending_ = true;
        } else {
            output_started = true;
            last_successful_fetch_us = fetch_return_us;
        }
        const bool recovered = !invalid && gap.began_us != 0 &&
                               gap.generation == generation && gap.epoch == afe_stream_epoch_;
        const unsigned output_feed_sequence = afe_feed_calls_;
        const unsigned output_feed_returns = afe_feed_returns_;
        const unsigned output_credits = static_cast<unsigned>(afe_credit_bytes_);
        const int64_t output_elapsed_ms = (esp_timer_get_time() - afe_started_us_) / 1000;
        if (recovered) {
            const auto producer = afe_producer_diagnostics_.Snapshot();
            const int64_t closed_us = esp_timer_get_time();
            xSemaphoreGive(mutex_);
#if defined(RODAKOS_RELEASE_TESTS)
            VoiceTickSnapshot tick_snapshot;
            tick_token = FreezeVoiceTickObservation(generation, epoch, tick_token,
                gap.first_stall, closed_us, false, tick_snapshot);
#endif
            LogAfeGapClosed(gap, "recovered", result_observed_us, last_fetch_return_us, fetch_calls,
                            output_feed_sequence, output_credits, producer, closed_us);
#if defined(RODAKOS_RELEASE_TESTS)
            LogVoiceTickObservation("recovered", tick_snapshot);
#endif
            gap = {};
        } else {
            xSemaphoreGive(mutex_);
        }
        if (first_output)
            ESP_LOGI(TAG, "AFE first output: generation=%u elapsed_ms=%lld feeds=%u returns=%u bytes=%d",
                     static_cast<unsigned>(generation), static_cast<long long>(output_elapsed_ms),
                     output_feed_sequence, output_feed_returns, result->data_size);
        if (invalid) {
            if (++fetch_errors == 1 || fetch_errors % 100 == 0)
                ESP_LOGW(TAG, "AFE fetch rejected: count=%u status=%d bytes=%d", fetch_errors,
                         result ? result->ret_value : ESP_FAIL, result ? result->data_size : 0);
        } else {
            const bool vad_valid = result->vad_state == VAD_SILENCE || result->vad_state == VAD_SPEECH;
            // Preserve silence; vad_cache duplicates pre-trigger history for other consumers.
            ProcessConversationSamples(result->data, result->data_size / sizeof(int16_t),
                                       generation, vad_valid, result->vad_state == VAD_SPEECH);
        }
        vTaskDelay(1);
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const unsigned feed_errors = afe_feed_errors_;
    const unsigned credits = static_cast<unsigned>(afe_credit_bytes_);
    const unsigned uncertain = static_cast<unsigned>(afe_uncertain_bytes_);
    xSemaphoreGive(mutex_);
    ESP_LOGI(TAG, "AFE fetch stopped: generation=%u current_failures=%u cancelled_results=%u",
             static_cast<unsigned>(generation), fetch_errors, cancelled_results);
    ESP_LOGI(TAG, "AFE flow stopped: generation=%u feed_errors=%u stalls=%u resyncs=%u resync_discarded=%u credits=%u uncertain=%u",
             static_cast<unsigned>(generation), feed_errors, stalls, resyncs, resync_discarded, credits, uncertain);
#if defined(RODAKOS_RELEASE_TESTS)
    RetireVoiceFeedProgress(generation);
    ObserveVoiceTickBoundary("flow_stop", generation, tick_epoch, tick_token, 0,
                            esp_timer_get_time(), true);
    LogVoiceFeedProgressSummary(generation);
#endif
    // The lifecycle owner joins and deletes this task before destroying AFE.
    vTaskSuspend(nullptr);
}

void VoiceAudioFrontend::SelectMainMicrophone(const std::vector<int16_t>& input,
                                              std::vector<int16_t>& output) {
    const size_t count = input.size() / 4;
    output.resize(count);
    int64_t p1 = 0, p2 = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t a = input[i * 4];
        const int32_t b = input[i * 4 + 2];
        p1 += static_cast<int64_t>(a) * a;
        p2 += static_cast<int64_t>(b) * b;
        output[i] = static_cast<int16_t>(selected_main_mic_ == 1 ? a : b);
    }
    const int64_t n = static_cast<int64_t>(std::max<size_t>(1, count));
    mic1_power_ = (mic1_power_ * 7 + (p1 / n) * 3) / 10;
    mic2_power_ = (mic2_power_ * 7 + (p2 / n) * 3) / 10;
    if (mic_speech_lock_) {
        if (std::max(mic1_power_, mic2_power_) < 25000) {
            mic_speech_lock_ = false;
            mic_switch_frames_ = 0;
        }
        return;
    }
    const int64_t current = selected_main_mic_ == 1 ? mic1_power_ : mic2_power_;
    const int64_t other = selected_main_mic_ == 1 ? mic2_power_ : mic1_power_;
    if (other > current + 180000) {
        if (++mic_switch_frames_ >= 5) {
            selected_main_mic_ = selected_main_mic_ == 1 ? 2 : 1;
            mic_switch_frames_ = 0;
            mic_speech_lock_ = true;
        }
    } else {
        mic_switch_frames_ = 0;
        if (std::max(mic1_power_, mic2_power_) > 25000) mic_speech_lock_ = true;
    }
}

void VoiceAudioFrontend::ProcessWakeSamples(std::vector<int16_t>& samples, uint32_t generation) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (mode_ != Mode::kWakeOnly || generation != wake_generation_ ||
        !wake_model_ready_ || multinet_ == nullptr || multinet_data_ == nullptr) {
        xSemaphoreGive(mutex_);
        return;
    }
    const esp_mn_state_t state = multinet_->detect(multinet_data_, samples.data());
    if (state == ESP_MN_STATE_TIMEOUT) {
        multinet_->clean(multinet_data_);
        xSemaphoreGive(mutex_);
        return;
    }
    if (state != ESP_MN_STATE_DETECTED) {
        xSemaphoreGive(mutex_);
        return;
    }

    esp_mn_results_t* results = multinet_->get_results(multinet_data_);
    bool detected = false;
    if (results != nullptr) {
        for (int i = 0; i < results->num; ++i) {
            if (results->command_id[i] == 1) {
                detected = true;
                break;
            }
        }
    }
    multinet_->clean(multinet_data_);
    if (!detected) {
        xSemaphoreGive(mutex_);
        return;
    }

    std::function<void(const std::string&)> callback;
    std::string detected_wake_word;
    uint32_t wake_generation = 0;
    if (mode_ == Mode::kWakeOnly) {
        mode_ = Mode::kIdle;
        callback = on_wake_word_;
        on_wake_word_ = {};
        wake_generation = wake_generation_;
        detected_wake_word = wake_identity_.wake_word;
    }
    xSemaphoreGive(mutex_);

    if (callback) {
        input_.CloseForOwner(kWakeAudioInputOwner);
        ESP_LOGI(TAG, "Wake word detected: %s", detected_wake_word.c_str());
        TaskHandle_t notification_task = nullptr;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool can_notify = wake_generation_ == wake_generation &&
                                wake_notification_task_ != nullptr &&
                                !wake_notification_pending_ &&
                                !pending_diagnostic_command_ &&
                                !wake_notification_active_;
        if (can_notify) {
            pending_wake_callback_ = std::move(callback);
            pending_wake_word_ = wake_identity_.wake_word;
            pending_wake_generation_ = wake_generation;
            wake_notification_pending_ = true;
            notification_task = wake_notification_task_;
        }
        xSemaphoreGive(mutex_);
        if (!can_notify) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            SetErrorLocked("Wake notification task unavailable");
            xSemaphoreGive(mutex_);
            ESP_LOGW(TAG,
                     "Wake notification task unavailable: internal_free=%u "
                     "internal_largest=%u psram_free=%u psram_largest=%u",
                     static_cast<unsigned>(
                         heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(
                         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(
                         heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(
                         heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        } else {
            xTaskNotifyGive(notification_task);
            ESP_LOGI(TAG,
                     "Wake notification dispatched: internal_free=%u largest=%u",
                     static_cast<unsigned>(
                         heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(
                         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        }
    }
}

void VoiceAudioFrontend::ProcessConversationSamples(const int16_t* samples, size_t count,
                                                     uint32_t generation, bool vad_valid,
                                                     bool vad_speech) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (mode_ != Mode::kConversation || generation != conversation_generation_) {
        xSemaphoreGive(mutex_);
        return;
    }
    const int64_t observed_us = esp_timer_get_time();
    aec_diagnostic_capture_.AppendAfe(samples, count, generation, observed_us);
    conversation_assembler_.Append(samples, count, recorder_config_, observed_us,
                                   vad_valid, vad_speech, frames_, kMaxQueuedFrames);
    xSemaphoreGive(mutex_);
}

size_t VoiceAudioFrontend::ResolveReadSamples(Mode mode) const {
    if (mode == Mode::kWakeOnly) {
        return wake_chunk_samples_;
    }
    if (mode == Mode::kConversation) {
        return kConversationReadSamples;
    }
    return 0;
}

void VoiceAudioFrontend::SetErrorLocked(const char* error) {
    last_error_ = error != nullptr && error[0] != '\0'
                      ? error
                      : "Voice audio frontend error";
    ESP_LOGW(TAG, "%s", last_error_.c_str());
}

void VoiceAudioFrontend::InvalidateAfeContinuityLocked() {
    conversation_assembler_.InvalidateContinuity();
    aec_diagnostic_capture_.MarkDiscontinuity(false, afe_generation_);
}

std::string VoiceAudioFrontend::LastErrorSnapshot() const {
    if (mutex_ == nullptr) return "Voice audio frontend error";
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const std::string snapshot = last_error_;
    xSemaphoreGive(mutex_);
    return snapshot;
}

}  // namespace rodakos
