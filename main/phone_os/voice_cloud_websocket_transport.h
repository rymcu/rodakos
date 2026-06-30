#pragma once

#include "phone_os/voice_assistant_transport.h"
#include "phone_os/voice_cloud_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

#include <esp_websocket_client.h>

#include <string>

namespace rodakos {

class VoiceCloudWebSocketTransport final : public VoiceAssistantTransport {
public:
    explicit VoiceCloudWebSocketTransport(VoiceCloudConfigService& config_service);
    ~VoiceCloudWebSocketTransport() override;

    bool Start() override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpen() const override;

    bool SendAudio(const VoiceAudioPacket& packet) override;
    bool SendStartListening(VoiceListeningMode mode) override;
    bool SendStopListening() override;
    bool SendWakeWordDetected(const std::string& wake_word) override;
    bool SendAbortSpeaking(VoiceAbortReason reason) override;
    bool SendMcpMessage(const std::string& payload) override;

    const char* name() const override { return "voice-cloud"; }
    const char* last_error() const override { return last_error_.c_str(); }

private:
    static void EventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    bool SendText(const std::string& text);
    bool SendHello();
    std::string BuildHelloMessage() const;
    void HandleTextFrame(const char* data, int len);
    void ParseServerHello(const std::string& payload);
    void SetError(const std::string& message);

    VoiceCloudConfigService& config_service_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    EventGroupHandle_t events_ = nullptr;
    esp_websocket_client_handle_t client_ = nullptr;
    VoiceCloudConfig config_;
    std::string headers_;
    std::string authorization_header_;
    std::string protocol_version_header_;
    std::string device_id_header_;
    std::string client_id_header_;
    std::string session_id_;
    std::string last_error_ = "Not connected";
    int server_sample_rate_ = 24000;
    int server_frame_duration_ms_ = 60;
    bool started_ = false;
    bool connected_ = false;
    bool channel_open_ = false;
};

}  // namespace rodakos
