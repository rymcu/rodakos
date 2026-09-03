#include "phone_os/voice_audio_frontend.h"

#include "rodakos_adapters/audio_codec_input.h"

#include <algorithm>
#include <utility>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
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
constexpr const char* kWakeWordCommand = "ni hao da ke";
constexpr const char* kWakeWordDisplay = "你好达克";
constexpr int kWakeInputPriority = 10;
constexpr int kConversationInputPriority = 30;
constexpr uint32_t kSampleRate = 16000;
constexpr uint16_t kInputChannels = 4;
constexpr uint16_t kBitsPerSample = 16;
// BigSmart raw TDM order is MIC1, MIC3(reference), MIC2, MIC4.
constexpr uint16_t kMainMicTdmSlot = 2;
constexpr uint16_t kInputChannelMask = 1U << kMainMicTdmSlot;
constexpr int kInputGain = 30;
constexpr int kDetectionDurationMs = 3000;
constexpr float kDetectionThreshold = 0.2F;
constexpr size_t kConversationReadSamples = 320;
constexpr size_t kMaxQueuedFrames = 80;
constexpr TickType_t kIdleDelay = pdMS_TO_TICKS(20);
constexpr TickType_t kInputRetryDelay = pdMS_TO_TICKS(100);
constexpr int kIdleCloseIterations = 15;

}  // namespace

VoiceAudioFrontend::VoiceAudioFrontend(AudioCodecInput& input) : input_(input) {
    mutex_ = xSemaphoreCreateMutex();
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
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool VoiceAudioFrontend::Init() {
    if (mutex_ == nullptr) {
        return false;
    }

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
    if (mutex_ == nullptr) {
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    task_running_ = false;
    ++wake_generation_;
    mode_ = Mode::kIdle;
    on_wake_word_ = {};
    frames_.clear();
    conversation_samples_.clear();
    wake_notification_pending_ = false;
    pending_wake_callback_ = {};
    pending_wake_word_.clear();
    pending_wake_generation_ = 0;
    xSemaphoreGive(mutex_);

    input_.CloseForOwner(kWakeAudioInputOwner);
    input_.CloseForOwner(kConversationAudioInputOwner);

    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool stopped = task_ == nullptr &&
                             !wake_notification_active_ &&
                             !wake_notification_pending_;
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
}

bool VoiceAudioFrontend::StartListening(
    std::function<void(const std::string&)> on_wake_word) {
    if (!Init()) {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    ++wake_generation_;
    on_wake_word_ = std::move(on_wake_word);
    mode_ = Mode::kWakeOnly;
    frames_.clear();
    conversation_samples_.clear();
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
             kWakeWordDisplay, static_cast<unsigned>(kMainMicTdmSlot));
    return true;
}

void VoiceAudioFrontend::StopListening() {
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

    xSemaphoreTake(mutex_, portMAX_DELAY);
    recorder_config_ = config;
    mode_ = Mode::kConversation;
    frames_.clear();
    conversation_samples_.clear();
    const bool task_started = EnsureCaptureTaskLocked();
    xSemaphoreGive(mutex_);

    if (!task_started || !EnsureInputForMode(Mode::kConversation)) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (mode_ == Mode::kConversation) {
            mode_ = Mode::kIdle;
            frames_.clear();
            conversation_samples_.clear();
        }
        SetErrorLocked("Microphone unavailable for assistant session");
        xSemaphoreGive(mutex_);
        input_.CloseForOwner(kConversationAudioInputOwner);
        return false;
    }

    ESP_LOGI(TAG, "Conversation capture started: %d Hz, %d ms frames",
             config.sample_rate, config.frame_duration_ms);
    return true;
}

void VoiceAudioFrontend::Stop() {
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (mode_ == Mode::kConversation) {
        mode_ = Mode::kIdle;
    }
    frames_.clear();
    conversation_samples_.clear();
    xSemaphoreGive(mutex_);
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

void VoiceAudioFrontend::CaptureTaskEntry(void* arg) {
    static_cast<VoiceAudioFrontend*>(arg)->CaptureTask();
}

void VoiceAudioFrontend::WakeNotificationTaskEntry(void* arg) {
    auto* owner = static_cast<VoiceAudioFrontend*>(arg);
    if (owner == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        std::function<void(const std::string&)> callback;
        std::string wake_word;
        bool stopping = false;
        bool should_notify = false;
        if (owner->mutex_ != nullptr) {
            xSemaphoreTake(owner->mutex_, portMAX_DELAY);
            stopping = owner->wake_notification_stopping_;
            if (!stopping) {
                owner->wake_notification_active_ = true;
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
    if (esp_mn_commands_add(1, kWakeWordCommand) != ESP_OK) {
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

    last_error_.clear();
    ESP_LOGI(TAG, "Loaded custom wake command '%s' (%u samples per chunk)",
             kWakeWordCommand, static_cast<unsigned>(wake_chunk_samples_));
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
    const BaseType_t created = xTaskCreatePinnedToCore(
        CaptureTaskEntry, "voice_frontend", 8192, this, 4, &task_, 0);
#else
    const BaseType_t created = xTaskCreate(
        CaptureTaskEntry, "voice_frontend", 8192, this, 4, &task_);
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
                                             kInputChannelMask);
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
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool task_running = task_running_;
        const Mode mode = mode_;
        const size_t read_samples = ResolveReadSamples(mode);
        xSemaphoreGive(mutex_);

        if (!task_running) {
            break;
        }
        if (mode == Mode::kIdle || read_samples == 0) {
            ++idle_iterations;
            if (idle_iterations >= kIdleCloseIterations) {
                input_.CloseForOwner(kWakeAudioInputOwner);
                input_.CloseForOwner(kConversationAudioInputOwner);
                xSemaphoreTake(mutex_, portMAX_DELAY);
                input_open_ = false;
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

        if (mode == Mode::kWakeOnly) {
            ProcessWakeSamples(samples);
        } else if (mode == Mode::kConversation) {
            ProcessConversationSamples(samples);
        }
    }

    input_.CloseForOwner(kWakeAudioInputOwner);
    input_.CloseForOwner(kConversationAudioInputOwner);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    input_open_ = false;
    task_ = nullptr;
    xSemaphoreGive(mutex_);
    vTaskDelete(nullptr);
}

void VoiceAudioFrontend::ProcessWakeSamples(std::vector<int16_t>& samples) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (mode_ != Mode::kWakeOnly || multinet_ == nullptr || multinet_data_ == nullptr) {
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
        ESP_LOGI(TAG, "Wake word detected: %s", kWakeWordDisplay);
        TaskHandle_t notification_task = nullptr;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool can_notify = wake_generation_ == wake_generation &&
                                wake_notification_task_ != nullptr &&
                                !wake_notification_pending_ &&
                                !wake_notification_active_;
        if (can_notify) {
            pending_wake_callback_ = std::move(callback);
            pending_wake_word_ = kWakeWordDisplay;
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

void VoiceAudioFrontend::ProcessConversationSamples(const std::vector<int16_t>& samples) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (mode_ != Mode::kConversation) {
        xSemaphoreGive(mutex_);
        return;
    }

    conversation_samples_.insert(
        conversation_samples_.end(), samples.begin(), samples.end());
    const size_t frame_samples = static_cast<size_t>(
        recorder_config_.sample_rate * recorder_config_.frame_duration_ms / 1000);
    while (frame_samples > 0 && conversation_samples_.size() >= frame_samples) {
        VoicePcmFrame frame;
        frame.config = recorder_config_;
        frame.timestamp_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        frame.samples.assign(conversation_samples_.begin(),
                             conversation_samples_.begin() + frame_samples);
        conversation_samples_.erase(conversation_samples_.begin(),
                                    conversation_samples_.begin() + frame_samples);
        if (frames_.size() >= kMaxQueuedFrames) {
            frames_.pop_front();
        }
        frames_.push_back(std::move(frame));
    }
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
