#pragma once

#include "phone_os/voice_recorder_service.h"
#include "phone_os/voice_wake_service.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_mn_iface.h>
#include <model_path.h>

namespace rodakos {

class AudioCodecInput;

class VoiceAudioFrontend final : public VoiceWakeRuntime, public VoiceRecorderService {
public:
    explicit VoiceAudioFrontend(AudioCodecInput& input);
    ~VoiceAudioFrontend() override;

    bool Init() override;
    void Deinit() override;

    bool StartListening(std::function<void(const std::string&)> on_wake_word) override;
    void StopListening() override;
    bool IsListening() const override;
    bool IsAvailable() const override { return true; }

    bool Start(const VoiceRecorderConfig& config) override;
    void Stop() override;
    bool IsRunning() const override;
    bool PopFrame(VoicePcmFrame& frame) override;

    const char* name() const override { return "esp-sr-multinet"; }
    const char* last_error() const override { return last_error_.c_str(); }

private:
    enum class Mode {
        kIdle,
        kWakeOnly,
        kConversation,
    };

    static void CaptureTaskEntry(void* arg);
    static void WakeNotificationTaskEntry(void* arg);

    bool InitModelLocked();
    void ReleaseModelLocked();
    bool EnsureCaptureTaskLocked();
    bool EnsureWakeNotificationTaskLocked();
    bool EnsureInputForMode(Mode mode);
    void CaptureTask();
    void ProcessWakeSamples(std::vector<int16_t>& samples);
    void ProcessConversationSamples(const std::vector<int16_t>& samples);
    size_t ResolveReadSamples(Mode mode) const;
    void SetErrorLocked(const char* error);

    AudioCodecInput& input_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    TaskHandle_t wake_notification_task_ = nullptr;
    bool wake_notification_stopping_ = false;
    bool wake_notification_active_ = false;
    bool wake_notification_pending_ = false;
    std::function<void(const std::string&)> pending_wake_callback_;
    std::string pending_wake_word_;
    uint32_t pending_wake_generation_ = 0;
    bool initialized_ = false;
    bool task_running_ = false;
    bool input_open_ = false;
    Mode mode_ = Mode::kIdle;
    VoiceRecorderConfig recorder_config_;
    std::function<void(const std::string&)> on_wake_word_;
    std::deque<VoicePcmFrame> frames_;
    std::vector<int16_t> conversation_samples_;
    srmodel_list_t* models_ = nullptr;
    esp_mn_iface_t* multinet_ = nullptr;
    model_iface_data_t* multinet_data_ = nullptr;
    bool commands_allocated_ = false;
    size_t wake_chunk_samples_ = 0;
    uint32_t wake_generation_ = 0;
    std::string last_error_;
};

}  // namespace rodakos
