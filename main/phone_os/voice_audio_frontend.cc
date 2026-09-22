#include "phone_os/voice_audio_frontend.h"

#include "rodakos_adapters/audio_codec_input.h"

#include <algorithm>
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
    Deinit();
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
    if (deinitializing_) return false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (initialized_) {
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

    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool stopped = task_ == nullptr;
        xSemaphoreGive(mutex_);
        if (stopped) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

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

    ESP_LOGI(TAG, "Always-on wake monitoring armed for %s on TDM slot %u (MIC2)",
             wake_identity_.wake_word.c_str(), static_cast<unsigned>(kMainMicTdmSlot));
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
    const VoiceIdentityConfig previous = wake_identity_;
    if (multinet_data_ == nullptr || multinet_ == nullptr) {
        wake_identity_ = normalized;
        xSemaphoreGive(mutex_);
        return true;
    }

    const bool updated = esp_mn_commands_clear() == ESP_OK &&
                         esp_mn_commands_add(1, normalized.wake_command.c_str()) == ESP_OK &&
                         esp_mn_commands_update() == nullptr;
    if (!updated) {
        esp_mn_commands_clear();
        esp_mn_commands_add(1, previous.wake_command.c_str());
        esp_mn_commands_update();
        SetErrorLocked("MultiNet rejected voice identity command");
        xSemaphoreGive(mutex_);
        return false;
    }
    wake_identity_ = normalized;
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
    if (multinet_data_ != nullptr) {
        return true;
    }

    models_ = srmodel_load(rodakos_voice_models_start);
    if (models_ == nullptr) {
        SetErrorLocked("Embedded speech model unavailable");
        return false;
    }
    if (models_->num <= 0) {
        SetErrorLocked("Embedded speech model list is empty");
        ReleaseModelLocked();
        return false;
    }

    char* model_name = esp_srmodel_filter(models_, ESP_MN_PREFIX, "cn");
    if (model_name == nullptr) {
        SetErrorLocked("Chinese MultiNet model unavailable");
        ReleaseModelLocked();
        return false;
    }

    multinet_ = esp_mn_handle_from_name(model_name);
    if (multinet_ == nullptr) {
        SetErrorLocked("MultiNet runtime unavailable");
        ReleaseModelLocked();
        return false;
    }

    multinet_data_ = multinet_->create(model_name, kDetectionDurationMs);
    if (multinet_data_ == nullptr) {
        SetErrorLocked("MultiNet initialization failed");
        ReleaseModelLocked();
        return false;
    }

    multinet_->set_det_threshold(multinet_data_, kDetectionThreshold);
    if (esp_mn_commands_alloc(multinet_, multinet_data_) != ESP_OK) {
        SetErrorLocked("MultiNet command registry allocation failed");
        ReleaseModelLocked();
        return false;
    }
    commands_allocated_ = true;
    if (esp_mn_commands_add(1, wake_identity_.wake_command.c_str()) != ESP_OK) {
        SetErrorLocked("Wake command registration failed");
        ReleaseModelLocked();
        return false;
    }
    if (esp_mn_commands_update() != nullptr) {
        SetErrorLocked("Wake command is unsupported by the speech model");
        ReleaseModelLocked();
        return false;
    }
    wake_chunk_samples_ = static_cast<size_t>(multinet_->get_samp_chunksize(multinet_data_));
    if (wake_chunk_samples_ == 0) {
        SetErrorLocked("Invalid MultiNet audio chunk size");
        ReleaseModelLocked();
        return false;
    }

    return true;
}

bool VoiceAudioFrontend::StartAfe(uint32_t generation) {
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
    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_ ? afe_iface_->create_from_config(afe_config) : nullptr;
    afe_config_free(afe_config);
    if (afe_data_ == nullptr) { afe_iface_ = nullptr; SetErrorLocked("AFE initialization failed"); xSemaphoreGive(mutex_); return false; }
    if (afe_iface_->get_feed_chunksize(afe_data_) <= 0 ||
        afe_iface_->get_feed_channel_num(afe_data_) != 2) {
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
    if (xTaskCreatePinnedToCoreWithCaps(AfeFetchTaskEntry, "afe_fetch", 6144, this, 4,
                                &afe_fetch_task_, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        afe_iface_->destroy(afe_data_); afe_data_ = nullptr; afe_iface_ = nullptr;
        SetErrorLocked("AFE fetch task creation failed"); xSemaphoreGive(mutex_); return false;
    }
    xSemaphoreGive(mutex_);
    return true;
}

void VoiceAudioFrontend::ReleaseModelLocked() {
    if (multinet_data_ != nullptr && multinet_ != nullptr) {
        multinet_->destroy(multinet_data_);
    }
    if (commands_allocated_) {
        esp_mn_commands_free();
        commands_allocated_ = false;
    }
    multinet_data_ = nullptr;
    multinet_ = nullptr;
    wake_chunk_samples_ = 0;
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

    task_running_ = true;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    const BaseType_t created = xTaskCreatePinnedToCoreWithCaps(
        CaptureTaskEntry, "voice_frontend", 8192, this, 4, &task_, 0,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    const BaseType_t created = xTaskCreateWithCaps(
        CaptureTaskEntry, "voice_frontend", 8192, this, 4, &task_,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (created != pdPASS) {
        task_running_ = false;
        task_ = nullptr;
        SetErrorLocked("Voice capture task creation failed");
        return false;
    }
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
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool task_running = task_running_;
        const Mode mode = mode_;
        const uint32_t generation = conversation_generation_;
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
        if (!EnsureInputForMode(mode)) {
            vTaskDelay(kInputRetryDelay);
            continue;
        }

        std::vector<int16_t> samples(read_samples);
        const char* owner = mode == Mode::kConversation
                                ? kConversationAudioInputOwner
                                : kWakeAudioInputOwner;
        if (!input_.ReadForOwner(owner,
                                 samples.data(),
                                 static_cast<int>(samples.size() * sizeof(int16_t)))) {
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

        const int64_t raw_observed_us = esp_timer_get_time();
        std::vector<int16_t> selected_samples;
        SelectMainMicrophone(samples, selected_samples);
        if (mode == Mode::kConversation) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            const bool can_feed = mode_ == mode && generation == conversation_generation_ &&
                                  afe_data_ != nullptr && !afe_fetch_stopping_;
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
            }
            xSemaphoreGive(mutex_);
            if (can_feed) {
                if (feed_generation != generation) {
                    afe_feed_buffer.clear();
                    feed_generation = generation;
                }
                for (size_t i = 0; i < samples.size() / kInputChannels; ++i) {
                    afe_feed_buffer.push_back(selected_samples[i]);
                    afe_feed_buffer.push_back(samples[i * 4 + 1]);
                }
                const size_t feed_size = static_cast<size_t>(afe_iface_->get_feed_chunksize(afe_data_)) * 2;
                while (feed_size > 0 && afe_feed_buffer.size() >= feed_size) {
                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    afe_feed_started_ = true;
                    xSemaphoreGive(mutex_);
                    afe_iface_->feed(afe_data_, afe_feed_buffer.data());
                    afe_feed_buffer.erase(afe_feed_buffer.begin(), afe_feed_buffer.begin() + feed_size);
                }
                xSemaphoreTake(mutex_, portMAX_DELAY);
                afe_feed_active_ = false;
                xSemaphoreGive(mutex_);
            }
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
    vTaskDeleteWithCaps(nullptr);
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
    unsigned fetch_errors = 0;
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool stopping = afe_fetch_stopping_;
        const bool fed = afe_feed_started_;
        const uint32_t generation = afe_generation_;
        xSemaphoreGive(mutex_);
        if (stopping) break;
        if (!fed) {
            vTaskDelay(1);
            continue;
        }
        auto* result = afe_iface_->fetch_with_delay(afe_data_, pdMS_TO_TICKS(100));
        if (result == nullptr || result->ret_value != ESP_OK || result->data == nullptr ||
            result->data_size <= 0 || result->data_size % sizeof(int16_t) != 0) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (generation == conversation_generation_) {
                conversation_assembler_.InvalidateContinuity();
                aec_diagnostic_capture_.MarkDiscontinuity(false, generation);
            }
            xSemaphoreGive(mutex_);
            if (++fetch_errors == 1 || fetch_errors % 100 == 0) {
                ESP_LOGW(TAG, "AFE fetch rejected: count=%u status=%d bytes=%d", fetch_errors,
                         result ? result->ret_value : ESP_FAIL, result ? result->data_size : 0);
            }
        } else {
            const bool vad_valid = result->vad_state == VAD_SILENCE || result->vad_state == VAD_SPEECH;
            // Every fetch frame, including silence, is retained. vad_cache repeats the
            // pre-trigger history needed only by consumers that discard non-speech.
            ProcessConversationSamples(result->data, result->data_size / sizeof(int16_t),
                                       generation, vad_valid, result->vad_state == VAD_SPEECH);
        }
        vTaskDelay(1);
    }
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
        multinet_ == nullptr || multinet_data_ == nullptr) {
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
    uint32_t wake_generation = 0;
    if (mode_ == Mode::kWakeOnly) {
        mode_ = Mode::kIdle;
        callback = on_wake_word_;
        on_wake_word_ = {};
        wake_generation = wake_generation_;
    }
    xSemaphoreGive(mutex_);

    if (callback) {
        input_.CloseForOwner(kWakeAudioInputOwner);
        ESP_LOGI(TAG, "Wake word detected: %s", wake_identity_.wake_word.c_str());
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

}  // namespace rodakos
