#include "phone_os/voice_assistant_service.h"

#include <inttypes.h>

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceAssistantService";
constexpr const char* kFocusOwner = "voice-assistant";

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

VoiceAssistantService::VoiceAssistantService(AudioFocusService& audio_focus)
    : audio_focus_(audio_focus) {
    mutex_ = xSemaphoreCreateMutex();
}

VoiceAssistantService::~VoiceAssistantService() {
    Deinit();
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
    if (!initialized_) {
        initialized_ = true;
        phase_ = VoiceAssistantPhase::kIdle;
        message_ = "Ready";
    }
    xSemaphoreGive(mutex_);
    return true;
}

void VoiceAssistantService::Deinit() {
    StopInteraction();
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        initialized_ = false;
        phase_ = VoiceAssistantPhase::kIdle;
        message_ = "Stopped";
        xSemaphoreGive(mutex_);
    }
}

bool VoiceAssistantService::StartInteraction(VoiceAssistantTrigger trigger,
                                             const std::string& wake_word) {
    if (!Init()) {
        return false;
    }

    if (mutex_ == nullptr) {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (focus_active_) {
        trigger_ = trigger;
        last_wake_word_ = wake_word;
        SetPhaseLocked(VoiceAssistantPhase::kListening, "Listening");
        xSemaphoreGive(mutex_);
        return true;
    }
    xSemaphoreGive(mutex_);

    AudioFocusRequest request;
    request.owner = kFocusOwner;
    request.gain = AudioFocusGain::kExclusive;
    request.resume_on_release = true;

    uint32_t token = 0;
    if (!audio_focus_.RequestFocus(request, token)) {
        MarkError("Audio focus unavailable");
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    focus_active_ = true;
    focus_token_ = token;
    trigger_ = trigger;
    last_wake_word_ = wake_word;
    SetPhaseLocked(VoiceAssistantPhase::kListening,
                   trigger == VoiceAssistantTrigger::kWakeWord ? "Wake word accepted" : "Listening");
    xSemaphoreGive(mutex_);

    ESP_LOGI(TAG, "Interaction started: trigger=%s focus_token=%" PRIu32,
             TriggerName(trigger), token);
    return true;
}

void VoiceAssistantService::StopInteraction() {
    uint32_t token = 0;
    bool should_release = false;

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        token = focus_token_;
        should_release = focus_active_;
        focus_active_ = false;
        focus_token_ = 0;
        phase_ = VoiceAssistantPhase::kIdle;
        message_ = "Ready";
        xSemaphoreGive(mutex_);
    }

    ReleaseFocusIfNeeded(token, should_release);
}

void VoiceAssistantService::MarkConnecting(const char* message) {
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    SetPhaseLocked(VoiceAssistantPhase::kConnecting, message != nullptr ? message : "Connecting");
    xSemaphoreGive(mutex_);
}

void VoiceAssistantService::MarkListening(const char* message) {
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    SetPhaseLocked(VoiceAssistantPhase::kListening, message != nullptr ? message : "Listening");
    xSemaphoreGive(mutex_);
}

void VoiceAssistantService::MarkSpeaking(const char* message) {
    if (mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    SetPhaseLocked(VoiceAssistantPhase::kSpeaking, message != nullptr ? message : "Speaking");
    xSemaphoreGive(mutex_);
}

void VoiceAssistantService::MarkError(const char* message) {
    uint32_t token = 0;
    bool should_release = false;

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        token = focus_token_;
        should_release = focus_active_;
        focus_active_ = false;
        focus_token_ = 0;
        SetPhaseLocked(VoiceAssistantPhase::kError, message != nullptr ? message : "Error");
        xSemaphoreGive(mutex_);
    }

    ReleaseFocusIfNeeded(token, should_release);
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
    state.focus_active = focus_active_;
    state.focus_token = focus_token_;
    state.message = message_;
    state.last_wake_word = last_wake_word_;
    xSemaphoreGive(mutex_);
    return state;
}

void VoiceAssistantService::SetPhaseLocked(VoiceAssistantPhase phase, const char* message) {
    phase_ = phase;
    if (message != nullptr) {
        message_ = message;
    }
}

void VoiceAssistantService::ReleaseFocusIfNeeded(uint32_t token, bool should_release) {
    if (should_release && token != 0) {
        audio_focus_.ReleaseFocus(token);
        ESP_LOGI(TAG, "Interaction stopped: focus_token=%" PRIu32, token);
    }
}

}  // namespace rodakos
