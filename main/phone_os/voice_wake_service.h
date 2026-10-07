#pragma once

#include "phone_os/voice_assistant_service.h"
#include "phone_os/voice_identity.h"
#include "phone_os/task-retirement.h"

#include <cstdint>
#include <functional>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace rodakos {

enum class VoiceWakeStatus {
    kDisabled,
    kListening,
    kAssistantActive,
    kUnavailable,
    kError,
};

struct VoiceWakeState {
    bool enabled = false;
    bool runtime_available = false;
    bool listening = false;
    VoiceWakeStatus status = VoiceWakeStatus::kDisabled;
    std::string runtime_name;
    std::string message;
    std::string last_wake_word;
    VoiceIdentityConfig voice_identity;
    std::string voice_identity_status;
    std::string voice_identity_error;
    uint32_t voice_identity_revision_watermark = 0;
    bool voice_identity_active_confirmed = false;
};

struct VoiceIdentityClockSnapshot {
    bool unix_valid = false;
    int64_t unix_ms = 0;
    int64_t monotonic_ms = 0;
};
using VoiceIdentityClock = std::function<VoiceIdentityClockSnapshot()>;

class VoiceWakeRuntime {
public:
    virtual ~VoiceWakeRuntime() = default;

    virtual bool Init() = 0;
    virtual void Deinit() = 0;
    virtual bool StartListening(std::function<void(const std::string&)> on_wake_word) = 0;
    virtual void StopListening() = 0;
    virtual bool IsListening() const = 0;
    virtual bool IsAvailable() const = 0;
    virtual bool ConfigureWakeWord(const VoiceIdentityConfig& config) = 0;
    virtual const char* name() const = 0;
    virtual const char* last_error() const = 0;
    // Concurrent runtimes override this to copy under their own state lock.
    virtual std::string LastErrorSnapshot() const { return last_error(); }
};

class UnavailableVoiceWakeRuntime final : public VoiceWakeRuntime {
public:
    bool Init() override;
    void Deinit() override;
    bool StartListening(std::function<void(const std::string&)> on_wake_word) override;
    void StopListening() override;
    bool IsListening() const override;
    bool IsAvailable() const override { return false; }
    bool ConfigureWakeWord(const VoiceIdentityConfig& config) override;
    const char* name() const override { return "wake-runtime"; }
    const char* last_error() const override { return last_error_.c_str(); }

private:
    std::string last_error_ = "Wake-word runtime not installed";
};

class VoiceWakeService {
public:
    VoiceWakeService(VoiceAssistantService& assistant, VoiceWakeRuntime& runtime,
                     VoiceIdentityClock clock = {});
    ~VoiceWakeService();

    bool Init();
    void Deinit();
    bool Start();
    void Stop();
    bool SetEnabled(bool enabled);
    bool IsEnabled();
    VoiceWakeState GetState();
    bool ApplyVoiceIdentity(const VoiceIdentityConfig& config, std::string& error);
    void RejectVoiceIdentity(const std::string& error);

    void NotifyWakeWordDetected(const std::string& wake_word);

private:
    static void SupervisorTask(void* arg);

    bool LoadSettingsLocked();
    bool SaveSettings(bool enabled);
    void EnsureSupervisorTaskLocked();
    void StopService(bool deinitialize);
    void FinishStopOperation(uint64_t epoch, bool supervisor_joined);
    void SupervisorTick();
    void LogHealthIfDueLocked();
    void HandleWakeWordDetected(const std::string& wake_word, uint32_t enable_generation);
    bool StartRuntimeLocked();
    bool StartConfiguredRuntimeLocked();
    void StopRuntimeLocked(const char* message);
    void SetStatusLocked(VoiceWakeStatus status, const char* message);
    bool ConfigureIdentityLocked(const VoiceIdentityConfig& config, std::string& error);
    bool ApplyIdentityRecordLocked(const VoiceIdentityRecord& next, const char* status,
                                   std::string& error, bool expiry = false);
    bool ReconcileIdentityLocked(bool allow_initialization = false);
    void FreezeIdentityLocked(const std::string& error);
    bool IsTemporaryExpiredLocked(const VoiceIdentityConfig& config,
                                  const VoiceIdentityClockSnapshot& clock);

    VoiceAssistantService& assistant_;
    VoiceWakeRuntime& runtime_;
    SemaphoreHandle_t mutex_ = nullptr;
    bool initialized_ = false;
    bool destroying_ = false;
    bool service_stopping_ = false;
    bool deinit_pending_ = false;
    uint64_t stop_epoch_ = 0;
    uint64_t completed_stop_epoch_ = 0;
    TaskHandle_t stop_owner_ = nullptr;
    bool task_running_ = false;
    bool enabled_ = false;
    bool listening_ = false;
    bool assistant_starting_ = false;
    uint32_t enable_generation_ = 0;
    uint32_t assistant_start_generation_ = 0;
    TickType_t assistant_active_since_ticks_ = 0;
    TickType_t last_health_log_ticks_ = 0;
    TaskHandle_t task_ = nullptr;
    TaskRetirementOwner retirement_owner_;
    TaskRetirementTicket supervisor_ticket_;
    VoiceWakeStatus status_ = VoiceWakeStatus::kDisabled;
    std::string message_ = "Disabled";
    std::string last_wake_word_;
    VoiceIdentityClock identity_clock_;
    VoiceIdentityRecord identity_record_;
    VoiceIdentityConfig runtime_identity_ = DefaultVoiceIdentityConfig();
    bool runtime_identity_configured_ = false;
    bool identity_recovery_required_ = false;
    bool identity_waiting_clock_ = false;
    struct IdentityDeadline {
        bool armed = false;
        uint32_t revision = 0;
        int64_t expires_at_ms = 0;
        int64_t monotonic_start_ms = 0;
        int64_t remaining_ms = 0;
    } identity_deadline_;
    std::string voice_identity_status_ = "rejected";
    std::string voice_identity_error_;
};

}  // namespace rodakos
