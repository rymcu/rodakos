#pragma once

#include "phone_os/voice_assistant_transport.h"
#include "phone_os/device_cloud_config.h"
#include "phone_os/realtime_voice_contract.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_websocket_client.h>

#include <string>

namespace rodakos {

class RodakRealtimeVoiceTransport final : public VoiceAssistantTransport {
public:
    explicit RodakRealtimeVoiceTransport(DeviceCloudConfigService& config_service);
    ~RodakRealtimeVoiceTransport() override;

    bool Start() override;
    bool OpenAudioChannel(VoiceOpenGuard can_continue = {}) override;
    void CloseAudioChannel() override;
    void WaitForAudioChannelClosed() override;
    bool IsAudioChannelOpen() const override;
    uint32_t connection_generation() const override;

    bool SendAudio(const VoiceAudioPacket& packet, uint32_t expected_generation) override;
    bool SendStartListening(VoiceListeningMode mode, uint32_t expected_generation) override;
    bool SendStopListening(uint32_t expected_generation) override;
    bool SendWakeWordDetected(const std::string& wake_word,
                              uint32_t expected_generation) override;
    bool SendAbortSpeaking(VoiceAbortReason reason, uint32_t expected_generation,
                          uint32_t playback_epoch = 0) override;
    bool SendVadStart(const char* source, uint32_t sequence,
                      uint32_t trigger_ms, uint32_t expected_generation,
                      uint32_t playback_epoch = 0) override;
    bool SendVadEnd(const char* source, uint32_t sequence,
                    uint32_t trigger_ms, uint32_t expected_generation,
                    uint32_t playback_epoch = 0) override;
    bool SendMcpMessage(const std::string& payload, uint32_t expected_generation) override;
    void SetInboundHandler(VoiceInboundHandler handler) override;

    const char* name() const override { return "rodak-realtime-voice"; }
    std::string last_error() const override;

private:
    bool SendVadState(const char* state, const char* source, uint32_t sequence,
                      uint32_t trigger_ms, uint32_t expected_generation, uint32_t playback_epoch);
    static void EventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void CleanupTaskEntry(void* arg);

    bool SendText(const std::string& text,
                  uint32_t expected_generation,
                  const std::string& expected_session,
                  bool require_open);
    bool SendSessionOpen(uint32_t generation);
    bool SnapshotSession(uint32_t expected_generation, std::string& session_id) const;
    std::string BuildSessionOpenMessage() const;
    void CleanupDetachedClient();
    void HandleDataFrame(const esp_websocket_event_data_t& data, uint32_t generation);
    void HandleTextFrame(const char* data, int len, uint32_t generation);
    void HandleBinaryFrame(const uint8_t* data, size_t size, uint32_t generation);
    void ParseSessionReady(const std::string& payload, uint32_t generation);
    void EmitInbound(VoiceInboundEvent&& event, uint32_t generation);
    void SetError(const std::string& message);
    bool IsConnectionCurrent(uint32_t generation) const;

    DeviceCloudConfigService& config_service_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    mutable SemaphoreHandle_t client_mutex_ = nullptr;
    SemaphoreHandle_t open_mutex_ = nullptr;
    SemaphoreHandle_t frame_mutex_ = nullptr;
    EventGroupHandle_t events_ = nullptr;
    esp_websocket_client_handle_t client_ = nullptr;
    esp_websocket_client_handle_t cleanup_client_ = nullptr;
    DeviceCloudConfig config_;
    std::string headers_;
    std::string authorization_header_;
    std::string protocol_version_header_;
    std::string device_id_header_;
    std::string client_id_header_;
    std::string session_id_;
    RealtimeVoiceSessionGate session_gate_;
    std::string last_error_ = "Not connected";
    VoiceInboundHandler inbound_handler_;
    std::vector<uint8_t> inbound_frame_;
    uint8_t inbound_opcode_ = 0;
    size_t inbound_frame_offset_ = 0;
    bool inbound_message_active_ = false;
    int server_sample_rate_ = 24000;
    int server_frame_duration_ms_ = 60;
    bool started_ = false;
    bool connected_ = false;
    bool channel_open_ = false;
    bool closing_ = false;
    bool cleanup_in_progress_ = false;
    bool cleanup_inline_required_ = false;
    bool cleanup_task_finished_ = false;
    TaskHandle_t cleanup_task_ = nullptr;
    uint32_t connection_generation_ = 0;
    uint32_t inbound_playback_epoch_ = 0;
    bool inbound_output_active_ = false;
    uint32_t audio_sequence_ = 0;
    uint32_t inbound_audio_sequence_ = 0;
    std::string vad_strategy_ = "server-authoritative";
};

}  // namespace rodakos
