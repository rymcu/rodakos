#pragma once

#include "phone_os/voice_assistant_service.h"

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
};

class VoiceWakeRuntime {
public:
    virtual ~VoiceWakeRuntime() = default;

    virtual bool Init() = 0;
    virtual void Deinit() = 0;
    virtual bool StartListening(std::function<void(const std::string&)> on_wake_word) = 0;
    virtual void StopListening() = 0;
    virtual bool IsListening() const = 0;
    virtual bool IsAvailable() const = 0;
    virtual const char* name() const = 0;
    virtual const char* last_error() const = 0;
};

class UnavailableVoiceWakeRuntime final : public VoiceWakeRuntime {
public:
    bool Init() override;
    void Deinit() override;
    bool StartListening(std::function<void(const std::string&)> on_wake_word) override;
    void StopListening() override;
    bool IsListening() const override;
    bool IsAvailable() const override { return false; }
    const char* name() const override { return "wake-runtime"; }
    const char* last_error() const override { return last_error_.c_str(); }

private:
    std::string last_error_ = "Wake-word runtime not installed";
};

class VoiceWakeService {
public:
    VoiceWakeService(VoiceAssistantService& assistant, VoiceWakeRuntime& runtime);
    ~VoiceWakeService();

    bool Init();
    void Deinit();
    bool Start();
    void Stop();
    bool SetEnabled(bool enabled);
    bool IsEnabled();
    VoiceWakeState GetState();

    void NotifyWakeWordDetected(const std::string& wake_word);

private:
    static void SupervisorTask(void* arg);

    void LoadSettingsLocked();
    void SaveSettings(bool enabled);
    void EnsureSupervisorTaskLocked();
    void SupervisorTick();
    bool StartRuntimeLocked();
    void StopRuntimeLocked(const char* message);
    void SetStatusLocked(VoiceWakeStatus status, const char* message);

    VoiceAssistantService& assistant_;
    VoiceWakeRuntime& runtime_;
    SemaphoreHandle_t mutex_ = nullptr;
    bool initialized_ = false;
    bool task_running_ = false;
    bool enabled_ = false;
    bool listening_ = false;
    TickType_t assistant_active_since_ticks_ = 0;
    TaskHandle_t task_ = nullptr;
    VoiceWakeStatus status_ = VoiceWakeStatus::kDisabled;
    std::string message_ = "Disabled";
    std::string last_wake_word_;
};

}  // namespace rodakos
