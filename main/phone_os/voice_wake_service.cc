#include "phone_os/voice_wake_service.h"

#include "phone_os/voice_wake_settings.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/idf_additions.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceWakeService";
constexpr TickType_t kSupervisorIntervalTicks = pdMS_TO_TICKS(1000);
constexpr TickType_t kAssistantSessionTimeoutTicks = pdMS_TO_TICKS(120000);

const char* StatusMessage(VoiceWakeStatus status) {
    switch (status) {
        case VoiceWakeStatus::kListening:
            return "Listening";
        case VoiceWakeStatus::kAssistantActive:
            return "Assistant active";
        case VoiceWakeStatus::kUnavailable:
            return "Wake runtime unavailable";
        case VoiceWakeStatus::kError:
            return "Wake listener error";
        case VoiceWakeStatus::kDisabled:
        default:
            return "Disabled";
    }
}

}  // namespace

bool UnavailableVoiceWakeRuntime::Init() {
    last_error_ = "Wake-word runtime not installed";
    return false;
}

void UnavailableVoiceWakeRuntime::Deinit() {
}

bool UnavailableVoiceWakeRuntime::StartListening(std::function<void(const std::string&)>) {
    last_error_ = "Wake-word runtime not installed";
    ESP_LOGW(TAG, "%s", last_error_.c_str());
    return false;
}

void UnavailableVoiceWakeRuntime::StopListening() {
}

bool UnavailableVoiceWakeRuntime::IsListening() const {
    return false;
}

VoiceWakeService::VoiceWakeService(VoiceAssistantService& assistant, VoiceWakeRuntime& runtime)
    : assistant_(assistant), runtime_(runtime) {
    mutex_ = xSemaphoreCreateMutex();
}

VoiceWakeService::~VoiceWakeService() {
    Deinit();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool VoiceWakeService::Init() {
    if (mutex_ == nullptr) {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (service_stopping_) {
        xSemaphoreGive(mutex_);
        return false;
    }
    if (!initialized_) {
        if (!LoadSettingsLocked()) {
            SetStatusLocked(VoiceWakeStatus::kError, "Failed to load wake setting");
            xSemaphoreGive(mutex_);
            ESP_LOGE(TAG, "Failed to load or initialize wake listener setting");
            return false;
        }
        initialized_ = true;
        SetStatusLocked(enabled_ ? VoiceWakeStatus::kUnavailable : VoiceWakeStatus::kDisabled,
                        enabled_ ? "Wake runtime unavailable" : "Disabled");
    }
    xSemaphoreGive(mutex_);
    return true;
}

void VoiceWakeService::Deinit() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (service_stopping_) {
            xSemaphoreGive(mutex_);
            while (true) {
                xSemaphoreTake(mutex_, portMAX_DELAY);
                const bool complete = !service_stopping_;
                xSemaphoreGive(mutex_);
                if (complete) {
                    Deinit();
                    return;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        service_stopping_ = true;
        ++enable_generation_;
        initialized_ = false;
        task_running_ = false;
        StopRuntimeLocked(enabled_ ? "Stopped" : "Disabled");
        xSemaphoreGive(mutex_);
    }
    WaitForSupervisorStop();

    assistant_.StopInteraction();
    runtime_.Deinit();
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        service_stopping_ = false;
        xSemaphoreGive(mutex_);
    }
}

bool VoiceWakeService::Start() {
    if (!Init()) {
        return false;
    }

    bool started = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (service_stopping_) {
        started = false;
    } else if (!enabled_) {
        SetStatusLocked(VoiceWakeStatus::kDisabled, "Disabled");
        started = true;
    } else {
        EnsureSupervisorTaskLocked();
        started = StartRuntimeLocked();
    }
    xSemaphoreGive(mutex_);
    return started;
}

void VoiceWakeService::Stop() {
    if (mutex_ == nullptr) {
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (service_stopping_) {
        xSemaphoreGive(mutex_);
        return;
    }
    service_stopping_ = true;
    ++enable_generation_;
    task_running_ = false;
    StopRuntimeLocked(enabled_ ? "Stopped" : "Disabled");
    xSemaphoreGive(mutex_);
    WaitForSupervisorStop();
    assistant_.StopInteraction();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    service_stopping_ = false;
    xSemaphoreGive(mutex_);
}

bool VoiceWakeService::SetEnabled(bool enabled) {
    if (!Init()) {
        return false;
    }

    bool active = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (service_stopping_) {
        xSemaphoreGive(mutex_);
        return false;
    }
    if (!SaveSettings(enabled)) {
        SetStatusLocked(VoiceWakeStatus::kError, "Failed to save wake setting");
        xSemaphoreGive(mutex_);
        ESP_LOGE(TAG, "Failed to persist wake listener setting");
        return false;
    }
    ++enable_generation_;
    enabled_ = enabled;
    if (!enabled_) {
        service_stopping_ = true;
        StopRuntimeLocked("Disabled");
        SetStatusLocked(VoiceWakeStatus::kDisabled, "Disabled");
    } else {
        EnsureSupervisorTaskLocked();
        active = StartRuntimeLocked();
    }
    xSemaphoreGive(mutex_);
    if (!enabled) {
        assistant_.StopInteraction();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        service_stopping_ = false;
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(TAG, "Wake listener %s", enabled ? "enabled" : "disabled");
    return !enabled || active;
}

bool VoiceWakeService::IsEnabled() {
    if (!Init()) {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool enabled = enabled_;
    xSemaphoreGive(mutex_);
    return enabled;
}

VoiceWakeState VoiceWakeService::GetState() {
    VoiceWakeState state;
    if (!Init()) {
        state.status = VoiceWakeStatus::kError;
        state.message = "Wake service unavailable";
        return state;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    state.enabled = enabled_;
    state.runtime_available = runtime_.IsAvailable();
    state.listening = listening_ && runtime_.IsListening();
    state.status = status_;
    state.runtime_name = runtime_.name();
    state.message = message_;
    state.last_wake_word = last_wake_word_;
    xSemaphoreGive(mutex_);
    return state;
}

void VoiceWakeService::NotifyWakeWordDetected(const std::string& wake_word) {
    uint32_t enable_generation = 0;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        enable_generation = enable_generation_;
        xSemaphoreGive(mutex_);
    }
    HandleWakeWordDetected(wake_word, enable_generation);
}

void VoiceWakeService::HandleWakeWordDetected(const std::string& wake_word,
                                              uint32_t enable_generation) {
    bool should_start = false;
    std::string detected = wake_word.empty() ? "wake word" : wake_word;

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (initialized_ && task_running_ && enabled_ && listening_ &&
            !service_stopping_ && !assistant_starting_ &&
            enable_generation_ == enable_generation) {
            runtime_.StopListening();
            listening_ = false;
            assistant_starting_ = true;
            assistant_start_generation_ = enable_generation;
            last_wake_word_ = detected;
            SetStatusLocked(VoiceWakeStatus::kAssistantActive, "Wake word detected");
            should_start = true;
        }
        xSemaphoreGive(mutex_);
    }

    if (!should_start) {
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    should_start = initialized_ && task_running_ && enabled_ && !service_stopping_ &&
                   enable_generation_ == enable_generation;
    if (!should_start && assistant_starting_ &&
        assistant_start_generation_ == enable_generation) {
        assistant_starting_ = false;
    }
    xSemaphoreGive(mutex_);
    if (!should_start) {
        return;
    }

    ESP_LOGI(TAG, "Wake word detected: %s", detected.c_str());
    const auto can_start = [this, enable_generation]() {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool current = initialized_ && task_running_ && enabled_ &&
                             !service_stopping_ &&
                             enable_generation_ == enable_generation;
        xSemaphoreGive(mutex_);
        return current;
    };
    uint32_t assistant_generation = 0;
    if (!assistant_.StartInteraction(
            VoiceAssistantTrigger::kWakeWord, detected, can_start, &assistant_generation)) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (assistant_starting_ && assistant_start_generation_ == enable_generation) {
            assistant_starting_ = false;
        }
        assistant_active_since_ticks_ = 0;
        if (initialized_ && task_running_ && enabled_ &&
            enable_generation_ == enable_generation) {
            SetStatusLocked(VoiceWakeStatus::kError, "Assistant start failed");
        }
        xSemaphoreGive(mutex_);
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool keep_active = initialized_ && task_running_ && enabled_ &&
                             !service_stopping_ && enable_generation_ == enable_generation;
    if (assistant_starting_ && assistant_start_generation_ == enable_generation) {
        assistant_starting_ = false;
    }
    if (keep_active) {
        assistant_active_since_ticks_ = xTaskGetTickCount();
    }
    xSemaphoreGive(mutex_);
    if (!keep_active) {
        assistant_.StopInteractionIfCurrent(assistant_generation);
    }
}

void VoiceWakeService::SupervisorTask(void* arg) {
    auto* self = static_cast<VoiceWakeService*>(arg);
    while (self != nullptr) {
        bool running = false;
        if (self->mutex_ != nullptr) {
            xSemaphoreTake(self->mutex_, portMAX_DELAY);
            running = self->task_running_;
            xSemaphoreGive(self->mutex_);
        }
        if (!running) {
            break;
        }

        self->SupervisorTick();
        vTaskDelay(kSupervisorIntervalTicks);
    }

    if (self != nullptr && self->mutex_ != nullptr) {
        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        self->task_ = nullptr;
        xSemaphoreGive(self->mutex_);
    }
    vTaskDeleteWithCaps(nullptr);
}

bool VoiceWakeService::LoadSettingsLocked() {
    enabled_ = false;
    if (!LoadVoiceWakeSettings(enabled_)) {
        return false;
    }
    ESP_LOGI(TAG, "Wake listener setting restored from NVS: %s",
             enabled_ ? "enabled" : "disabled");
    return true;
}

bool VoiceWakeService::SaveSettings(bool enabled) {
    return SaveVoiceWakeSettings(enabled);
}

void VoiceWakeService::EnsureSupervisorTaskLocked() {
    if (task_ != nullptr) {
        return;
    }

    task_running_ = true;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    const BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        SupervisorTask, "voice_wake", 4096, this, 2, &task_, 0,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    const BaseType_t ret = xTaskCreateWithCaps(
        SupervisorTask, "voice_wake", 4096, this, 2, &task_,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (ret != pdPASS) {
        task_ = nullptr;
        task_running_ = false;
        SetStatusLocked(VoiceWakeStatus::kError, "Wake supervisor failed");
        ESP_LOGW(TAG, "Failed to start wake supervisor task");
    }
}

void VoiceWakeService::WaitForSupervisorStop() {
    if (mutex_ == nullptr) {
        return;
    }
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool stopped = task_ == nullptr;
        xSemaphoreGive(mutex_);
        if (stopped) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void VoiceWakeService::SupervisorTick() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!initialized_ || service_stopping_ || !task_running_) {
        xSemaphoreGive(mutex_);
        return;
    }
    if (!enabled_) {
        StopRuntimeLocked("Disabled");
        xSemaphoreGive(mutex_);
        return;
    }
    if (listening_ && !runtime_.IsListening()) {
        listening_ = false;
        SetStatusLocked(VoiceWakeStatus::kError, runtime_.last_error());
        ESP_LOGW(TAG, "Wake runtime stopped capturing; re-arming");
    }
    if (assistant_starting_) {
        SetStatusLocked(VoiceWakeStatus::kAssistantActive, "Assistant starting");
        xSemaphoreGive(mutex_);
        return;
    }

    const auto assistant_state = assistant_.GetState();

    if (assistant_state.phase == VoiceAssistantPhase::kError) {
        assistant_active_since_ticks_ = 0;
        SetStatusLocked(VoiceWakeStatus::kError,
                        assistant_state.message.empty() ? "Assistant failed" : assistant_state.message.c_str());
        xSemaphoreGive(mutex_);
        assistant_.StopInteraction();
        return;
    }

    if (assistant_state.stopping || assistant_state.focus_active ||
        assistant_state.phase != VoiceAssistantPhase::kIdle) {
        const TickType_t now = xTaskGetTickCount();
        if (assistant_active_since_ticks_ == 0) {
            assistant_active_since_ticks_ = now;
        } else if ((now - assistant_active_since_ticks_) >= kAssistantSessionTimeoutTicks) {
            assistant_active_since_ticks_ = 0;
            SetStatusLocked(VoiceWakeStatus::kError, "Assistant session timed out");
            xSemaphoreGive(mutex_);
            assistant_.StopInteraction();
            return;
        }
        if (listening_) {
            StopRuntimeLocked("Assistant active");
        }
        SetStatusLocked(VoiceWakeStatus::kAssistantActive, "Assistant active");
        xSemaphoreGive(mutex_);
        return;
    }

    assistant_active_since_ticks_ = 0;
    if (!listening_) {
        StartRuntimeLocked();
    }
    xSemaphoreGive(mutex_);
}

bool VoiceWakeService::StartRuntimeLocked() {
    if (!initialized_ || service_stopping_ || !task_running_ || !enabled_ ||
        assistant_starting_) {
        listening_ = false;
        return false;
    }
    if (!runtime_.IsAvailable()) {
        listening_ = false;
        SetStatusLocked(VoiceWakeStatus::kUnavailable, runtime_.last_error());
        return false;
    }

    if (!runtime_.Init()) {
        listening_ = false;
        SetStatusLocked(VoiceWakeStatus::kError, runtime_.last_error());
        return false;
    }

    const uint32_t enable_generation = enable_generation_;
    const bool started = runtime_.StartListening([this, enable_generation](const std::string& wake_word) {
        HandleWakeWordDetected(wake_word, enable_generation);
    });
    if (!started) {
        listening_ = false;
        SetStatusLocked(VoiceWakeStatus::kError, runtime_.last_error());
        return false;
    }

    listening_ = true;
    SetStatusLocked(VoiceWakeStatus::kListening, "Listening");
    return true;
}

void VoiceWakeService::StopRuntimeLocked(const char* message) {
    if (listening_) {
        runtime_.StopListening();
    }
    listening_ = false;
    if (status_ == VoiceWakeStatus::kListening) {
        SetStatusLocked(VoiceWakeStatus::kDisabled, message);
    }
}

void VoiceWakeService::SetStatusLocked(VoiceWakeStatus status, const char* message) {
    status_ = status;
    message_ = message != nullptr && message[0] != '\0' ? message : StatusMessage(status);
}

}  // namespace rodakos
