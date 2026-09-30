#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_peer.h"

namespace rodakos {

class CameraService;

/**
 * Owns one ESP peer connection used to publish the camera as JPEG frames over
 * an ordered WebRTC data channel. Signalling is intentionally supplied by the
 * caller so this class can be used by MQTT, HTTP or a future Rodak gateway.
 */
class WebRtcCameraService {
public:
    using SignalingCallback =
        std::function<void(esp_peer_msg_type_t type, std::vector<uint8_t>&& payload)>;
    using StateCallback = std::function<void(esp_peer_state_t state)>;

    struct Config {
        int width = 320;
        int height = 240;
        uint8_t fps = 5;
        uint16_t chunk_size = 10000;
        esp_peer_role_t role = ESP_PEER_ROLE_CONTROLLING;
    };

    explicit WebRtcCameraService(CameraService* camera_service);
    ~WebRtcCameraService();

    WebRtcCameraService(const WebRtcCameraService&) = delete;
    WebRtcCameraService& operator=(const WebRtcCameraService&) = delete;

    // Starts preview, opens esp_peer, creates the JPEG channel and emits local SDP.
    bool Start(const Config& config, SignalingCallback on_signaling, StateCallback on_state = {});
    void Stop();

    // Feed the SDP or trickled ICE candidate received from the remote peer.
    bool HandleRemoteMessage(esp_peer_msg_type_t type, const uint8_t* data, size_t size);
    bool HandleRemoteMessage(esp_peer_msg_type_t type, const std::vector<uint8_t>& data) {
        return HandleRemoteMessage(type, data.data(), data.size());
    }

    bool IsRunning() const;
    bool IsChannelOpen() const;

private:
    static int OnPeerState(esp_peer_state_t state, void* ctx);
    static int OnPeerMessage(esp_peer_msg_t* message, void* ctx);
    static int OnChannelOpen(esp_peer_data_channel_info_t* channel, void* ctx);
    static int OnChannelClose(esp_peer_data_channel_info_t* channel, void* ctx);
    static void PeerTaskEntry(void* arg);

    void PeerTask();
    void RequestStop(esp_peer_state_t state);
    void FinishStop();
    void HandleJpeg(std::vector<uint8_t>&& jpeg, uint32_t sequence, int64_t timestamp_us);
    bool SendJpegChunks(const std::vector<uint8_t>& jpeg);
    void EmitState(esp_peer_state_t state);
    void EmitMessage(esp_peer_msg_t* message);

    CameraService* camera_service_ = nullptr;
    // esp_peer_new_connection may synchronously emit the local SDP callback.
    mutable std::recursive_mutex mutex_;
    // Protect a borrowed peer handle against close while MQTT/JPEG sends finish.
    // The default esp_peer implementation is single-threaded: main_loop,
    // signalling, channel creation and data sends must not overlap close.
    // Recursive ownership is required because callbacks run inline from
    // main_loop and may create a data channel.
    std::recursive_mutex peer_api_mutex_;
    Config config_{};
    SignalingCallback signaling_callback_;
    StateCallback state_callback_;
    esp_peer_handle_t peer_ = nullptr;
    TaskHandle_t peer_task_ = nullptr;
    // xTaskCreateWithCaps may schedule the task before publishing its output
    // handle. Keep the task parked until Start has stored the handle.
    bool peer_task_ready_ = false;
    bool stop_requested_ = false;
    esp_peer_state_t terminal_state_ = ESP_PEER_STATE_CLOSED;
    bool running_ = false;
    bool channel_open_ = false;
    bool channel_requested_ = false;
};

}  // namespace rodakos
