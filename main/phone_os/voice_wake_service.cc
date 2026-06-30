#include "phone_os/voice_wake_service.h"

#include "settings.h"

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceWakeService";
constexpr const char* kSettingsNamespace = "voice_wake";
constexpr const char* kEnabledKey = "enabled";
constexpr TickType_t kSupervisorIntervalTicks = pdMS_TO_TICKS(1000);
constexpr TickType_t kAssistantSessionTimeoutTicks = pdMS_TO_TICKS(30000);

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
    if (!initialized_) {
        LoadSettingsLocked();
        initialized_ = true;
        SetStatusLocked(enabled_ ? VoiceWakeStatus::kUnavailable : VoiceWakeStatus::kDisabled,
                        enabled_ ? "Wake runtime unavailable" : "Disabled");
    }
    xSemaphoreGive(mutex_);
    return true;
}

void VoiceWakeService::Deinit() {
    TaskHandle_t task = nullptr;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        task_running_ = false;
        task = task_;
        xSemaphoreGive(mutex_);
    }
    for (int i = 0; task != nullptr && i < 150; ++i) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool stopped = task_ == nullptr;
        xSemaphoreGive(mutex_);
        if (stopped) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    Stop();
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        initialized_ = false;
        xSemaphoreGive(mutex_);
    }
    runtime_.Deinit();
}

bool VoiceWakeService::Start() {
    if (!Init()) {
        return false;
    }

    bool started = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!enabled_) {
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
    StopRuntimeLocked(enabled_ ? "Stopped" : "Disabled");
    xSemaphoreGive(mutex_);
}

bool VoiceWakeService::SetEnabled(bool enabled) {
    if (!Init()) {
        return false;
    }

    bool active = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    enabled_ = enabled;
    SaveSettings(enabled_);
    if (!enabled_) {
        StopRuntimeLocked("Disabled");
        SetStatusLocked(VoiceWakeStatus::kDisabled, "Disabled");
    } else {
        EnsureSupervisorTaskLocked();
        active = StartRuntimeLocked();
    }
    xSemaphoreGive(mutex_);
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
    bool should_start = false;
    std::string detected = wake_word.empty() ? "wake word" : wake_word;

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (enabled_) {
            runtime_.StopListening();
            listening_ = false;
            last_wake_word_ = detected;
            SetStatusLocked(VoiceWakeStatus::kAssistantActive, "Wake word detected");
            should_start = true;
        }
        xSemaphoreGive(mutex_);
    }

    if (!should_start) {
        return;
    }

    ESP_LOGI(TAG, "Wake word detected: %s", detected.c_str());
    if (!assistant_.StartInteraction(VoiceAssistantTrigger::kWakeWord, detected)) {
        assistant_.StopInteraction();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        assistant_active_since_ticks_ = 0;
        SetStatusLocked(VoiceWakeStatus::kError, "Assistant start failed");
        xSemaphoreGive(mutex_);
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    assistant_active_since_ticks_ = xTaskGetTickCount();
    xSemaphoreGive(mutex_);
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
    vTaskDelete(nullptr);
}

void VoiceWakeService::LoadSettingsLocked() {
    Settings settings(kSettingsNamespace, false);
    enabled_ = settings.GetBool(kEnabledKey, false);
}

void VoiceWakeService::SaveSettings(bool enabled) {
    Settings settings(kSettingsNamespace, true);
    settings.SetBool(kEnabledKey, enabled);
}

void VoiceWakeService::EnsureSupervisorTaskLocked() {
    if (task_ != nullptr) {
        task_running_ = true;
        return;
    }

    task_running_ = true;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    const BaseType_t ret = xTaskCreatePinnedToCore(
        SupervisorTask, "voice_wake", 4096, this, 2, &task_, 0);
#else
    const BaseType_t ret = xTaskCreate(SupervisorTask, "voice_wake", 4096, this, 2, &task_);
#endif
    if (ret != pdPASS) {
        task_ = nullptr;
        task_running_ = false;
        SetStatusLocked(VoiceWakeStatus::kError, "Wake supervisor failed");
        ESP_LOGW(TAG, "Failed to start wake supervisor task");
    }
}

void VoiceWakeService::SupervisorTick() {
    const auto assistant_state = assistant_.GetState();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!enabled_) {
        StopRuntimeLocked("Disabled");
        xSemaphoreGive(mutex_);
        return;
    }

    if (assistant_state.phase == VoiceAssistantPhase::kError) {
        assistant_active_since_ticks_ = 0;
        SetStatusLocked(VoiceWakeStatus::kError,
                        assistant_state.message.empty() ? "Assistant failed" : assistant_state.message.c_str());
        xSemaphoreGive(mutex_);
        assistant_.StopInteraction();
        return;
    }

    if (assistant_state.focus_active || assistant_state.phase != VoiceAssistantPhase::kIdle) {
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

    const bool started = runtime_.StartListening([this](const std::string& wake_word) {
        NotifyWakeWordDetected(wake_word);
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
        SetStatusLocked(enabled_ ? VoiceWakeStatus::kUnavailable : VoiceWakeStatus::kDisabled, message);
    }
}

void VoiceWakeService::SetStatusLocked(VoiceWakeStatus status, const char* message) {
    status_ = status;
    message_ = message != nullptr && message[0] != '\0' ? message : StatusMessage(status);
}

}  // namespace rodakos
