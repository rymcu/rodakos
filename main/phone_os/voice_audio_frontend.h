#pragma once

#include "phone_os/voice_recorder_service.h"
#include "phone_os/voice_pcm_assembler.h"
#include "phone_os/voice_aec_diagnostic_capture.h"
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
#include <esp_afe_sr_iface.h>

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
    bool ConfigureWakeWord(const VoiceIdentityConfig& config) override;

    bool Start(const VoiceRecorderConfig& config) override;
    void Stop() override;
    bool IsRunning() const override;
    bool PopFrame(VoicePcmFrame& frame) override;

    // USB diagnostics reuse the internal-stack worker because voice startup accesses NVS.
    bool QueueDiagnosticCommand(std::function<void()> command);
    bool LoadDiagnosticAudio(const std::string& command);
    bool ArmDiagnosticAudio();
    bool ReplayDiagnosticAudio();
    void ClearDiagnosticAudio();
    bool ArmAecDiagnosticCapture(uint32_t duration_ms = 6000);
    void StopAecDiagnosticCapture();
    bool ClearAecDiagnosticCapture();
    VoiceAecDiagnosticCapture::Status GetAecDiagnosticCaptureStatus() const;
    bool ReadAecDiagnosticCaptureChunk(uint8_t channel, size_t offset, size_t count,
                                       std::string& hex) const;

    const char* name() const override { return "esp-sr-multinet"; }
    const char* last_error() const override { return last_error_.c_str(); }

private:
    enum class Mode {
        kIdle,
        kWakeOnly,
        kConversation,
    };

    static void CaptureTaskEntry(void* arg);
    static void AfeFetchTaskEntry(void* arg);
    static void WakeNotificationTaskEntry(void* arg);

    bool InitModelLocked();
    void ReleaseModelLocked();
    bool EnsureCaptureTaskLocked();
    bool EnsureWakeNotificationTaskLocked();
    bool EnsureInputForMode(Mode mode);
    void CaptureTask();
    bool StartAfe(uint32_t generation);
    void StopAfe();
    void AfeFetchTask();
    void ProcessWakeSamples(std::vector<int16_t>& samples, uint32_t generation);
    void ProcessConversationSamples(const int16_t* samples, size_t count, uint32_t generation,
                                    bool vad_valid, bool vad_speech);
    void SelectMainMicrophone(const std::vector<int16_t>& input, std::vector<int16_t>& output);
    size_t ResolveReadSamples(Mode mode) const;
    void SetErrorLocked(const char* error);

    AudioCodecInput& input_;
    VoiceAecDiagnosticCapture aec_diagnostic_capture_;
    SemaphoreHandle_t lifecycle_mutex_ = nullptr;
    bool deinitializing_ = false;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    TaskHandle_t wake_notification_task_ = nullptr;
    bool wake_notification_stopping_ = false;
    bool wake_notification_active_ = false;
    bool wake_notification_pending_ = false;
    std::function<void(const std::string&)> pending_wake_callback_;
    std::function<void()> pending_diagnostic_command_;
    int16_t* diagnostic_audio_ = nullptr;
    size_t diagnostic_audio_samples_ = 0;
    size_t diagnostic_audio_loaded_ = 0;
    size_t diagnostic_audio_position_ = 0;
    bool diagnostic_audio_active_ = false;
    std::string pending_wake_word_;
    uint32_t pending_wake_generation_ = 0;
    bool initialized_ = false;
    bool task_running_ = false;
    bool input_open_ = false;
    Mode mode_ = Mode::kIdle;
    VoiceRecorderConfig recorder_config_;
    std::function<void(const std::string&)> on_wake_word_;
    std::deque<VoicePcmFrame> frames_;
    VoicePcmAssembler conversation_assembler_;
    srmodel_list_t* models_ = nullptr;
    esp_mn_iface_t* multinet_ = nullptr;
    model_iface_data_t* multinet_data_ = nullptr;
    bool commands_allocated_ = false;
    size_t wake_chunk_samples_ = 0;
    uint32_t wake_generation_ = 0;
    std::string last_error_;
    VoiceIdentityConfig wake_identity_ = DefaultVoiceIdentityConfig();
    int selected_main_mic_ = 2;
    int mic_switch_frames_ = 0;
    int64_t mic1_power_ = 0;
    int64_t mic2_power_ = 0;
    bool mic_speech_lock_ = false;
    const esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    TaskHandle_t afe_fetch_task_ = nullptr;
    bool afe_fetch_stopping_ = false;
    uint32_t conversation_generation_ = 0;
    uint32_t afe_generation_ = 0;
    bool afe_feed_active_ = false;
    bool afe_feed_started_ = false;
};

}  // namespace rodakos
