#include "phone_os/voice_wake_service.h"

#include "phone_os/voice_wake_settings.h"
#include "phone_os/time_service.h"

#include <utility>
#include <cstdlib>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/idf_additions.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceWakeService";
constexpr TickType_t kSupervisorIntervalTicks = pdMS_TO_TICKS(1000);
constexpr TickType_t kAssistantSessionTimeoutTicks = pdMS_TO_TICKS(120000);
constexpr TickType_t kHealthLogIntervalTicks = pdMS_TO_TICKS(60000);

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

bool UnavailableVoiceWakeRuntime::ConfigureWakeWord(const VoiceIdentityConfig&) {
    last_error_ = "Wake-word runtime not installed";
    return false;
}

VoiceWakeService::VoiceWakeService(VoiceAssistantService& assistant, VoiceWakeRuntime& runtime,
                                   VoiceIdentityClock clock)
    : assistant_(assistant), runtime_(runtime), identity_clock_(std::move(clock)) {
    if (!identity_clock_) identity_clock_ = []() {
        VoiceIdentityClockSnapshot snapshot;
        snapshot.unix_valid = TimeServiceUnixTimeMs(snapshot.unix_ms);
        snapshot.monotonic_ms = esp_timer_get_time() / 1000;
        return snapshot;
    };
    mutex_ = xSemaphoreCreateMutex();
}

VoiceWakeService::~VoiceWakeService() {
    retirement_owner_.Close();
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (supervisor_ticket_.IsCurrentTask()) std::abort();
        destroying_ = true;
        xSemaphoreGive(mutex_);
    }
    Deinit();
    retirement_owner_.Drain();
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
    if (service_stopping_ || destroying_) {
        xSemaphoreGive(mutex_);
        return false;
    }
    if (!initialized_) {
        // A failed migration/read may already have changed storage. Ordinary
        // GetState/Start retries must not turn that uncertainty into success.
        if (identity_recovery_required_) { xSemaphoreGive(mutex_); return false; }
        if (!LoadSettingsLocked()) {
            SetStatusLocked(VoiceWakeStatus::kError, "Failed to load wake setting");
            xSemaphoreGive(mutex_);
            ESP_LOGE(TAG, "Failed to load or initialize wake listener setting");
            return false;
        }
        initialized_ = true;
        SetStatusLocked(enabled_ ? VoiceWakeStatus::kUnavailable : VoiceWakeStatus::kDisabled,
                        enabled_ ? "Wake runtime unavailable" : "Disabled");
        ReconcileIdentityLocked();
    }
    xSemaphoreGive(mutex_);
    return true;
}

void VoiceWakeService::Deinit() {
    StopService(true);
}

bool VoiceWakeService::Start() {
    if (!Init()) {
        return false;
    }

    bool started = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (service_stopping_ || !initialized_) {
        started = false;
    } else if (identity_recovery_required_) {
        started = false;
    } else {
        EnsureSupervisorTaskLocked();
        if (!enabled_) {
            SetStatusLocked(VoiceWakeStatus::kDisabled, "Disabled");
            started = task_running_ && ReconcileIdentityLocked();
        } else {
            started = task_running_ && StartRuntimeLocked();
        }
    }
    xSemaphoreGive(mutex_);
    return started;
}

void VoiceWakeService::Stop() {
    StopService(false);
}

void VoiceWakeService::StopService(bool deinitialize) {
    if (mutex_ == nullptr) return;
    TaskRetirementTicket ticket;
    const auto caller = xTaskGetCurrentTaskHandle();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    ticket = supervisor_ticket_;
    if (deinitialize) {
        deinit_pending_ = true;
        initialized_ = false;
    }
    task_running_ = false;
    if (service_stopping_) {
        const auto epoch = stop_epoch_;
        const bool reentrant = stop_owner_ == caller || ticket.IsCurrentTask();
        xSemaphoreGive(mutex_);
        if (reentrant) return;
        ticket.Join();
        // A later Start/Stop may already exist when this waiter is scheduled.
        // Its completion still belongs only to the operation captured above.
        for (;;) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            const bool complete = completed_stop_epoch_ >= epoch;
            xSemaphoreGive(mutex_);
            if (complete) return;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    service_stopping_ = true;
    stop_owner_ = caller;
    const auto epoch = ++stop_epoch_;
    ++enable_generation_;
    task_running_ = false;
    StopRuntimeLocked(enabled_ ? "Stopped" : "Disabled");
    xSemaphoreGive(mutex_);

    ticket.Join();
    assistant_.StopInteraction();
    FinishStopOperation(epoch, true);
}

void VoiceWakeService::FinishStopOperation(uint64_t epoch, bool supervisor_joined) {
    bool deinitialized = false;
    for (;;) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        // Disabling normally keeps the supervisor. A concurrent Stop/Deinit
        // can upgrade that operation before this completion is published.
        if (!task_running_ && !supervisor_joined) {
            const auto ticket = supervisor_ticket_;
            xSemaphoreGive(mutex_);
            ticket.Join();
            supervisor_joined = true;
            continue;
        }
        if (!deinit_pending_ || deinitialized) {
            deinit_pending_ = false;
            completed_stop_epoch_ = epoch;
            stop_owner_ = nullptr;
            service_stopping_ = false;
            xSemaphoreGive(mutex_);
            return;
        }
        xSemaphoreGive(mutex_);
        runtime_.Deinit();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        runtime_identity_configured_ = false;
        identity_recovery_required_ = false;
        identity_waiting_clock_ = false;
        identity_deadline_ = {};
        xSemaphoreGive(mutex_);
        deinitialized = true;
    }
}

bool VoiceWakeService::SetEnabled(bool enabled) {
    if (!Init()) {
        return false;
    }

    bool active = false;
    uint64_t stop_epoch = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (service_stopping_ || !initialized_) {
        xSemaphoreGive(mutex_);
        return false;
    }
    if (enabled && identity_recovery_required_) {
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
        stop_owner_ = xTaskGetCurrentTaskHandle();
        stop_epoch = ++stop_epoch_;
        StopRuntimeLocked("Disabled");
        SetStatusLocked(VoiceWakeStatus::kDisabled, "Disabled");
    } else {
        EnsureSupervisorTaskLocked();
        active = task_running_ && StartRuntimeLocked();
    }
    xSemaphoreGive(mutex_);
    if (!enabled) {
        assistant_.StopInteraction();
        FinishStopOperation(stop_epoch, false);
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
    const bool ready = Init();
    VoiceWakeState state;
    if (mutex_ == nullptr) {
        state.status = VoiceWakeStatus::kError;
        state.message = "Wake service unavailable";
        return state;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (ready && !service_stopping_) ReconcileIdentityLocked();
    state.enabled = enabled_;
    state.runtime_available = runtime_.IsAvailable();
    state.listening = listening_ && runtime_.IsListening();
    state.status = ready && initialized_ ? status_ : VoiceWakeStatus::kError;
    state.runtime_name = runtime_.name();
    state.message = message_;
    state.last_wake_word = last_wake_word_;
    state.voice_identity = runtime_identity_;
    state.voice_identity_status = voice_identity_status_;
    state.voice_identity_error = voice_identity_error_;
    state.voice_identity_revision_watermark = identity_record_.last_accepted.revision;
    state.voice_identity_active_confirmed = initialized_ && !service_stopping_ && runtime_identity_configured_ &&
        !identity_recovery_required_ && !identity_waiting_clock_;
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

    if (mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool current = initialized_ && task_running_ && enabled_ && !service_stopping_ &&
        !identity_recovery_required_ && enable_generation_ == enable_generation;
    xSemaphoreGive(mutex_);
    if (!current) return;

    // AEC/VAD frontends may report a wake phrase while TTS is active. Treat it
    // as a barge-in on the existing session instead of opening a second one.
    const VoiceAssistantPhase assistant_phase = assistant_.GetPhaseSnapshot();
    if (assistant_phase == VoiceAssistantPhase::kSpeaking) {
        // The phase snapshot may have waited while Stop or an identity change invalidated
        // this callback. Keep that generation current through the short setter.
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool can_interrupt = initialized_ && task_running_ && enabled_ &&
            !service_stopping_ && !identity_recovery_required_ &&
            enable_generation_ == enable_generation;
        const bool interrupted = can_interrupt && assistant_.InterruptSpeaking();
        xSemaphoreGive(mutex_);
        if (interrupted) {
            ESP_LOGI(TAG, "Wake word interrupted active TTS: %s", detected.c_str());
        }
        return;
    }

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
}

bool VoiceWakeService::LoadSettingsLocked() {
    enabled_ = false;
    std::string error;
    if (!LoadVoiceWakeSettings(enabled_) ||
        !LoadVoiceWakeIdentitySettings(identity_record_, error)) {
        FreezeIdentityLocked(error.empty() ? "failed to load voice identity settings" : error);
        return false;
    }
    runtime_identity_configured_ = false;
    identity_waiting_clock_ = false;
    identity_deadline_ = {};
    voice_identity_status_ = "pending";
    voice_identity_error_ = "voice identity runtime is not configured";
    return true;
}

bool VoiceWakeService::SaveSettings(bool enabled) {
    return SaveVoiceWakeSettings(enabled);
}

void VoiceWakeService::EnsureSupervisorTaskLocked() {
    if (task_ != nullptr) {
        return;
    }

    const auto previous = supervisor_ticket_;
    auto ticket = ReserveTaskRetirement(retirement_owner_, SupervisorTask, this);
    if (!ticket) {
        task_running_ = false;
        SetStatusLocked(VoiceWakeStatus::kError, "Wake supervisor failed");
        return;
    }
    supervisor_ticket_ = ticket;
    task_running_ = true;
    last_health_log_ticks_ = 0;
    TaskHandle_t created = nullptr;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    const BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        TaskRetirementEntry, "voice_wake", 4096, TaskRetirementContext(ticket), 2, &created, 0,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    const BaseType_t ret = xTaskCreateWithCaps(
        TaskRetirementEntry, "voice_wake", 4096, TaskRetirementContext(ticket), 2, &created,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (ret != pdPASS || created == nullptr) {
        CancelTaskRetirement(ticket);
        supervisor_ticket_ = previous;
        task_ = nullptr;
        task_running_ = false;
        SetStatusLocked(VoiceWakeStatus::kError, "Wake supervisor failed");
        ESP_LOGW(TAG, "Failed to start wake supervisor task");
    } else {
        task_ = created;
        PublishTaskRetirement(ticket, created);
    }
}

void VoiceWakeService::LogHealthIfDueLocked() {
    const TickType_t now = xTaskGetTickCount();
    if (last_health_log_ticks_ != 0 &&
        (now - last_health_log_ticks_) < kHealthLogIntervalTicks) {
        return;
    }
    last_health_log_ticks_ = now;

    ESP_LOGI(TAG,
             "Voice health: enabled=%d status=%u listening=%d assistant_starting=%d "
             "internal_free=%u internal_min=%u internal_largest=%u "
             "psram_free=%u psram_min=%u psram_largest=%u "
             "supervisor_stack_min_free=%u",
             enabled_, static_cast<unsigned>(status_), listening_, assistant_starting_,
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
}

void VoiceWakeService::SupervisorTick() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!initialized_ || service_stopping_ || !task_running_) {
        xSemaphoreGive(mutex_);
        return;
    }
    if (!ReconcileIdentityLocked()) {
        xSemaphoreGive(mutex_);
        return;
    }
    if (!enabled_) {
        StopRuntimeLocked("Disabled");
        xSemaphoreGive(mutex_);
        return;
    }
    LogHealthIfDueLocked();
    if (listening_ && !runtime_.IsListening()) {
        listening_ = false;
        SetStatusLocked(VoiceWakeStatus::kError, runtime_.LastErrorSnapshot().c_str());
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
        if (assistant_state.phase == VoiceAssistantPhase::kSpeaking) {
            // TTS can legitimately exceed the idle-session budget. Refresh the
            // watchdog while playback is active so a streamed answer is not
            // truncated before the transport emits its terminal event.
            assistant_active_since_ticks_ = now;
        } else {
            if (assistant_active_since_ticks_ == 0) {
                assistant_active_since_ticks_ = now;
            } else if ((now - assistant_active_since_ticks_) >= kAssistantSessionTimeoutTicks) {
                assistant_active_since_ticks_ = 0;
                SetStatusLocked(VoiceWakeStatus::kError, "Assistant session timed out");
                ESP_LOGW(TAG, "Assistant session watchdog expired during phase=%u",
                         static_cast<unsigned>(assistant_state.phase));
                xSemaphoreGive(mutex_);
                assistant_.StopInteraction();
                return;
            }
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
    if (!ReconcileIdentityLocked(true)) return false;
    return StartConfiguredRuntimeLocked();
}

bool VoiceWakeService::StartConfiguredRuntimeLocked() {
    if (!initialized_ || service_stopping_ || !task_running_ || !enabled_ ||
        assistant_starting_ || identity_recovery_required_ || !runtime_identity_configured_) {
        listening_ = false;
        return false;
    }
    const uint32_t enable_generation = enable_generation_;
    const bool started = runtime_.StartListening([this, enable_generation](const std::string& wake_word) {
        HandleWakeWordDetected(wake_word, enable_generation);
    });
    if (!started) {
        runtime_.StopListening();
        listening_ = false;
        SetStatusLocked(VoiceWakeStatus::kError, runtime_.LastErrorSnapshot().c_str());
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

bool VoiceWakeService::ConfigureIdentityLocked(const VoiceIdentityConfig& config, std::string& error) {
    runtime_identity_configured_ = false;
    if (!runtime_.IsAvailable() || !runtime_.Init() || !runtime_.ConfigureWakeWord(config)) {
        error = runtime_.LastErrorSnapshot();
        if (error.empty()) error = "wake runtime rejected voice identity";
        return false;
    }
    runtime_identity_ = config;
    runtime_identity_configured_ = true;
    return true;
}

void VoiceWakeService::FreezeIdentityLocked(const std::string& error) {
    ++enable_generation_;
    runtime_.StopListening();
    listening_ = false;
    runtime_identity_configured_ = false;
    identity_recovery_required_ = true;
    voice_identity_status_ = "recovery_failed";
    voice_identity_error_ = error;
    SetStatusLocked(VoiceWakeStatus::kError, error.c_str());
}

bool VoiceWakeService::IsTemporaryExpiredLocked(const VoiceIdentityConfig& config,
                                               const VoiceIdentityClockSnapshot& clock) {
    if (config.mode != VoiceIdentityApplyMode::kTemporary) return false;
    if (clock.unix_valid && IsVoiceIdentityExpired(config, clock.unix_ms)) return true;
    if (!identity_deadline_.armed || identity_deadline_.revision != config.revision ||
        identity_deadline_.expires_at_ms != config.expires_at_ms) {
        if (!clock.unix_valid) return false;
        identity_deadline_ = {true, config.revision, config.expires_at_ms,
                              clock.monotonic_ms, config.expires_at_ms - clock.unix_ms};
    }
    // Only elapsed monotonic time is compared with a duration derived from a
    // trusted Unix snapshot. A backward wall-clock step cannot renew the lease.
    return clock.monotonic_ms >= identity_deadline_.monotonic_start_ms &&
        clock.monotonic_ms - identity_deadline_.monotonic_start_ms >= identity_deadline_.remaining_ms;
}

bool VoiceWakeService::ApplyIdentityRecordLocked(const VoiceIdentityRecord& next,
                                                const char* status, std::string& error,
                                                bool expiry) {
    const VoiceIdentityRecord previous = identity_record_;
    const VoiceIdentityConfig previous_runtime = runtime_identity_;
    const bool previous_confirmed = runtime_identity_configured_;
    const bool was_listening = listening_;
    const auto previous_deadline = identity_deadline_;
    const bool previous_waiting = identity_waiting_clock_;
    const bool record_changed = !VoiceIdentityConfigEquals(previous.persistent, next.persistent) ||
        !VoiceIdentityConfigEquals(previous.active, next.active) ||
        !VoiceIdentityConfigEquals(previous.last_accepted, next.last_accepted);
    if (!VoiceIdentityConfigEquals(previous.active, next.active)) {
        identity_deadline_ = {};
        if (next.active.mode == VoiceIdentityApplyMode::kTemporary &&
            IsTemporaryExpiredLocked(next.active, identity_clock_())) {
            identity_deadline_ = previous_deadline;
            error = "temporary voice identity expired before application";
            voice_identity_status_ = "rejected";
            voice_identity_error_ = error;
            return false;
        }
    }
    ++enable_generation_;
    runtime_.StopListening();
    listening_ = false;
    bool persisted = false;
    bool configured = ConfigureIdentityLocked(next.active, error);
    if (configured && record_changed) {
        const auto saved = SaveVoiceWakeIdentitySettings(next, error);
        if (saved == VoiceIdentitySaveStatus::kIndeterminate) {
            FreezeIdentityLocked(error.empty() ? "voice identity persistence is indeterminate" : error);
            return false;
        }
        persisted = saved == VoiceIdentitySaveStatus::kSaved;
        configured = persisted;
    }
    if (configured) {
        identity_record_ = next;
        identity_waiting_clock_ = false;
        if (next.active.mode == VoiceIdentityApplyMode::kTemporary &&
            IsTemporaryExpiredLocked(next.active, identity_clock_())) {
            VoiceIdentityRecord expired = next;
            expired.active = expired.persistent;
            if (!ApplyIdentityRecordLocked(expired, "expired", error, true)) return false;
            if (was_listening && !StartConfiguredRuntimeLocked()) {
                error = "wake listener could not resume after immediate identity expiry";
                FreezeIdentityLocked(error);
                return false;
            }
            return true;
        }
        if (!was_listening || StartConfiguredRuntimeLocked()) {
            voice_identity_status_ = status;
            voice_identity_error_.clear();
            error.clear();
            return true;
        }
        error = runtime_.LastErrorSnapshot();
        if (error.empty()) error = "wake listener could not restart after identity change";
    }

    const std::string operation_error = error.empty() ? "voice identity change failed" : error;
    runtime_.StopListening();
    listening_ = false;
    std::string restore_error;
    const bool runtime_restored = previous_confirmed && ConfigureIdentityLocked(previous_runtime, restore_error);
    bool storage_restored = true;
    if (persisted) {
        storage_restored = SaveVoiceWakeIdentitySettings(previous, restore_error) == VoiceIdentitySaveStatus::kSaved;
    }
    if (storage_restored) identity_record_ = previous;
    identity_deadline_ = previous_deadline;
    identity_waiting_clock_ = previous_waiting;
    // A failed candidate may have consumed the remaining lifetime of the old
    // identity. Restoring its graph is not permission to restart an expired wake.
    const bool previous_expired = IsTemporaryExpiredLocked(previous_runtime, identity_clock_());
    const bool listener_restored = runtime_restored && storage_restored && !expiry && !previous_expired &&
        (!was_listening || StartConfiguredRuntimeLocked());
    if (!listener_restored) {
        error = operation_error + "; identity recovery failed";
        if (!restore_error.empty()) error += ": " + restore_error;
        FreezeIdentityLocked(error);
        return false;
    }
    voice_identity_status_ = "rejected";
    voice_identity_error_ = operation_error;
    error = operation_error;
    return false;
}

bool VoiceWakeService::ReconcileIdentityLocked(bool allow_initialization) {
    if (!initialized_ || identity_recovery_required_ || service_stopping_) return false;
    const auto clock = identity_clock_();
    if (!runtime_identity_configured_ && !allow_initialization) {
        bool changed = false;
        const bool was_waiting = identity_waiting_clock_;
        if (IsTemporaryExpiredLocked(identity_record_.active, clock)) {
            VoiceIdentityRecord expired = identity_record_;
            expired.active = expired.persistent;
            std::string error;
            if (SaveVoiceWakeIdentitySettings(expired, error) != VoiceIdentitySaveStatus::kSaved) {
                FreezeIdentityLocked(error.empty() ? "failed to persist identity expiry" : error);
                return false;
            }
            identity_record_ = expired;
            identity_deadline_ = {};
            changed = true;
        }
        identity_waiting_clock_ = identity_record_.active.mode == VoiceIdentityApplyMode::kTemporary &&
            !clock.unix_valid;
        runtime_identity_ = identity_waiting_clock_ ? identity_record_.persistent : identity_record_.active;
        if (changed || was_waiting != identity_waiting_clock_ || voice_identity_status_ != "rejected") {
            voice_identity_status_ = identity_waiting_clock_ ? "pending_clock" : "pending";
            voice_identity_error_ = identity_waiting_clock_ ? "Unix clock is not synchronized" :
                "voice identity runtime is not configured";
        }
        return true;
    }
    if (identity_record_.active.mode == VoiceIdentityApplyMode::kTemporary) {
        if (IsTemporaryExpiredLocked(identity_record_.active, clock)) {
            VoiceIdentityRecord expired = identity_record_;
            expired.active = expired.persistent;
            std::string error;
            return ApplyIdentityRecordLocked(expired, "expired", error, true);
        }
        if (!clock.unix_valid) {
            identity_waiting_clock_ = true;
            if (!runtime_identity_configured_ ||
                !VoiceIdentityConfigEquals(runtime_identity_, identity_record_.persistent)) {
                const bool was_listening = listening_;
                ++enable_generation_;
                runtime_.StopListening();
                listening_ = false;
                std::string error;
                if (!ConfigureIdentityLocked(identity_record_.persistent, error) ||
                    (was_listening && !StartConfiguredRuntimeLocked())) {
                    FreezeIdentityLocked(error.empty() ? "persistent identity fallback failed" : error);
                    return false;
                }
            }
            voice_identity_status_ = "pending_clock";
            voice_identity_error_ = "Unix clock is not synchronized";
            return true;
        }
    }
    const bool was_waiting = identity_waiting_clock_;
    identity_waiting_clock_ = false;
    if (!runtime_identity_configured_ ||
        !VoiceIdentityConfigEquals(runtime_identity_, identity_record_.active)) {
        const bool previously_expired = identity_record_.last_accepted.mode == VoiceIdentityApplyMode::kTemporary &&
            identity_record_.active.mode == VoiceIdentityApplyMode::kPersistent;
        std::string error;
        return ApplyIdentityRecordLocked(identity_record_, previously_expired ? "expired" : "applied", error);
    }
    if (was_waiting) {
        voice_identity_status_ = "applied";
        voice_identity_error_.clear();
    }
    return true;
}

void VoiceWakeService::RejectVoiceIdentity(const std::string& error) {
    Init();
    if (mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (initialized_ && !identity_recovery_required_ && !service_stopping_) {
        voice_identity_status_ = "rejected";
        voice_identity_error_ = error;
    }
    xSemaphoreGive(mutex_);
}

bool VoiceWakeService::ApplyVoiceIdentity(const VoiceIdentityConfig& config, std::string& error) {
    if (!Init()) { error = "voice identity service requires explicit recovery"; return false; }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const auto finish = [&](bool accepted) { xSemaphoreGive(mutex_); return accepted; };
    if (!initialized_ || service_stopping_ || identity_recovery_required_) {
        error = identity_recovery_required_ ? "voice identity requires explicit recovery" : "wake service is stopping";
        return finish(false);
    }
    VoiceIdentityConfig normalized;
    if (!NormalizeVoiceIdentityConfig(config, normalized, error)) {
        voice_identity_status_ = "rejected";
        voice_identity_error_ = error;
        return finish(false);
    }
    if (!ReconcileIdentityLocked(true)) {
        error = voice_identity_error_;
        return finish(false);
    }
    const auto& accepted = identity_record_.last_accepted;
    if (normalized.revision <= accepted.revision) {
        if (normalized.revision == accepted.revision && VoiceIdentityConfigEquals(normalized, accepted)) {
            if (!identity_waiting_clock_) {
                voice_identity_status_ = VoiceIdentityConfigEquals(identity_record_.active, accepted)
                    ? "applied" : "expired";
                voice_identity_error_.clear();
            }
            error.clear();
            return finish(true);
        }
        error = normalized.revision < accepted.revision ? "voice identity revision is stale" :
            "voice identity revision conflicts with the accepted request";
        voice_identity_status_ = "rejected";
        voice_identity_error_ = error;
        return finish(false);
    }
    const auto clock = identity_clock_();
    if (normalized.mode == VoiceIdentityApplyMode::kTemporary &&
        (!clock.unix_valid || IsVoiceIdentityExpired(normalized, clock.unix_ms))) {
        error = clock.unix_valid ? "temporary voice identity has expired" : "Unix clock is not synchronized";
        voice_identity_status_ = "rejected";
        voice_identity_error_ = error;
        return finish(false);
    }
    VoiceIdentityRecord next = identity_record_;
    if (normalized.mode == VoiceIdentityApplyMode::kPersistent) next.persistent = normalized;
    next.active = normalized;
    next.last_accepted = normalized;
    const bool applied = ApplyIdentityRecordLocked(next, "applied", error);
    return finish(applied);
}

}  // namespace rodakos
