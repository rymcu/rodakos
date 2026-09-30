#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_peer.h"

namespace rodakos {

class DisplayService;

/**
 * Owns one ESP peer connection used to publish the display as JPEG frames over
 * an ordered WebRTC data channel. Signalling is intentionally supplied by the
 * caller so this class can be used by MQTT, HTTP or a future Rodak gateway.
 */
class WebRtcDisplayService {
public:
    using SignalingCallback =
        std::function<void(esp_peer_msg_type_t type, std::vector<uint8_t>&& payload)>;
    using StateCallback = std::function<void(esp_peer_state_t state)>;
    using ControlReply = std::function<void(bool accepted, const char* reason)>;
    using ControlCallback = std::function<void(const std::string& payload, ControlReply reply)>;

    struct Config {
        int width = 320;
        int height = 240;
        uint8_t fps = 5;
        // 320x240 quality-65 JPEG 通常小于 20KB；使用更大的单片可减少
        // SCTP 分片之间的 main_loop 往返，降低画面发送延迟。底层仍会
        // 通过 SendJpegChunks 对超大 JPEG 自动拆分。
        uint16_t chunk_size = 20000;
        esp_peer_role_t role = ESP_PEER_ROLE_CONTROLLING;
    };

    explicit WebRtcDisplayService(DisplayService* display_service);
    ~WebRtcDisplayService();

    WebRtcDisplayService(const WebRtcDisplayService&) = delete;
    WebRtcDisplayService& operator=(const WebRtcDisplayService&) = delete;

    // Starts display capture, opens esp_peer, creates the JPEG channel and emits local SDP.
    bool Start(const Config& config, SignalingCallback on_signaling, StateCallback on_state = {},
               ControlCallback on_control = {});
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
    static int OnPeerData(esp_peer_data_frame_t* frame, void* ctx);
    static int OnChannelOpen(esp_peer_data_channel_info_t* channel, void* ctx);
    static int OnChannelClose(esp_peer_data_channel_info_t* channel, void* ctx);
    static void PeerTaskEntry(void* arg);

    void PeerTask();
    void RequestStop(esp_peer_state_t state);
    void FinishStop();
    void HandleJpeg(std::vector<uint8_t>&& jpeg, uint32_t sequence, int64_t timestamp_us);
    bool SendJpegChunks(const std::vector<uint8_t>& jpeg);
    void MaybeLogTransportStats();
    void EmitState(esp_peer_state_t state);
    void EmitMessage(esp_peer_msg_t* message);
    void HandleControlData(esp_peer_data_frame_t* frame);
    bool SendControlAck(uint32_t sequence, bool accepted, const char* reason = nullptr);
    void QueueControlAck(uint32_t sequence, bool accepted, const char* reason);
    void FlushControlAcks();

    DisplayService* display_service_ = nullptr;
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
    ControlCallback control_callback_;
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
    bool control_channel_requested_ = false;
    uint16_t video_stream_id_ = 0;
    uint16_t control_stream_id_ = 0;
    bool control_channel_open_ = false;
    uint32_t last_control_sequence_ = 0;
    int64_t last_move_at_us_ = 0;
    std::vector<uint8_t> pending_jpeg_;
    uint32_t pending_jpeg_sequence_ = 0;
    int64_t pending_jpeg_timestamp_us_ = 0;
    struct PendingControlAck {
        uint32_t sequence = 0;
        bool accepted = false;
        std::string reason;
    };
    std::deque<PendingControlAck> pending_control_acks_;

    // JPEG DataChannel 只在设备侧运行，使用原子计数避免为诊断日志扩大
    // peer_api_mutex_ / service mutex_ 的临界区。日志本身每 5 秒最多一条。
    std::atomic<uint64_t> stats_frames_received_{0};
    std::atomic<uint64_t> stats_frames_sent_{0};
    std::atomic<uint64_t> stats_frames_dropped_{0};
    std::atomic<uint64_t> stats_chunks_sent_{0};
    std::atomic<uint64_t> stats_bytes_sent_{0};
    std::atomic<uint64_t> stats_send_retries_{0};
    std::atomic<uint64_t> stats_send_would_block_{0};
    std::atomic<uint64_t> stats_send_errors_{0};
    std::atomic<uint64_t> stats_send_timeouts_{0};
    std::atomic<uint64_t> stats_send_duration_us_{0};
    std::atomic<uint64_t> stats_send_max_duration_us_{0};
    std::atomic<uint64_t> stats_api_lock_acquires_{0};
    std::atomic<uint64_t> stats_api_lock_wait_us_{0};
    std::atomic<uint64_t> stats_api_lock_max_wait_us_{0};
    std::atomic<int64_t> stats_next_log_at_us_{0};
};

}  // namespace rodakos
