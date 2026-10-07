#pragma once

#include <atomic>
#include <array>
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
#include "phone_os/display_control_ack_tracker.h"
#include "phone_os/stream_lease.h"
#include "phone_os/task-retirement.h"
#include "phone_os/webrtc-peer-resources.h"

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
        // Use the caller's revocable stream; Start creates no implicit grant.
        StreamLeasePtr stream_lease;
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
    struct DurationStats {
        uint64_t samples = 0;
        uint64_t total_us = 0;
        uint64_t maximum_us = 0;
        int64_t maximum_at_us = 0;
        void Observe(int64_t started_us, int64_t ended_us);
    };
    struct LoopDiagnostics {
        DurationStats gap;
        DurationStats service_wait;
        DurationStats api_wait;
        DurationStats sdk;
        DurationStats ack;
        DurationStats jpeg;
    };
    struct ControlTiming {
        uint64_t generation = 0;
        uint32_t sequence = 0;
        uint16_t stream_id = 0;
        int64_t entered_us = 0;
        int64_t dispatch_us = 0;
        int64_t returned_us = 0;
    };
    struct ScopedDuration {
        WebRtcDisplayService& owner;
        DurationStats LoopDiagnostics::* field;
        int64_t started_us;
        uint64_t generation;
        ScopedDuration(WebRtcDisplayService& service, DurationStats LoopDiagnostics::* observed);
        ~ScopedDuration();
    };
    static int OnPeerState(esp_peer_state_t state, void* ctx);
    static int OnPeerMessage(esp_peer_msg_t* message, void* ctx);
    static int OnPeerData(esp_peer_data_frame_t* frame, void* ctx);
    static int OnChannelOpen(esp_peer_data_channel_info_t* channel, void* ctx);
    static int OnChannelClose(esp_peer_data_channel_info_t* channel, void* ctx);
    static void PeerTaskEntry(void* arg);

    void PeerTask();
    int PumpPeer(esp_peer_handle_t peer);
    void RecordDuration(DurationStats LoopDiagnostics::* field, int64_t started_us,
                        int64_t ended_us, uint64_t generation);
    void MaybeLogTimings(bool force = false);
    void RequestStop(esp_peer_state_t state);
    void FinishStop();
    void HandleJpeg(std::vector<uint8_t>&& jpeg, uint32_t sequence, int64_t timestamp_us);
    bool SendJpegChunks(const std::vector<uint8_t>& jpeg);
    void MaybeLogTransportStats();
    void EmitState(esp_peer_state_t state);
    void EmitMessage(esp_peer_msg_t* message);
    void HandleControlData(esp_peer_data_frame_t* frame, int64_t entered_us);
    bool SendControlAck(const DisplayControlAckTracker::Ack& ack, int* error = nullptr);
    void QueueControlAck(const DisplayControlAckTracker::InstancePtr& instance,
                         uint32_t sequence, bool accepted, const char* reason);
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
    WebRtcPeerResources peer_resources_{};
    SignalingCallback signaling_callback_;
    StateCallback state_callback_;
    ControlCallback control_callback_;
    esp_peer_handle_t peer_ = nullptr;
    TaskRetirementOwner task_retirement_owner_;
    TaskRetirementTicket peer_retirement_;
    TaskHandle_t peer_task_ = nullptr;
    // xTaskCreateWithCaps may schedule the task before publishing its output
    // handle. Keep the task parked until Start has stored the handle.
    bool peer_task_ready_ = false;
    bool closing_ = false;
    bool stop_requested_ = false;
    esp_peer_state_t terminal_state_ = ESP_PEER_STATE_CLOSED;
    bool running_ = false;
    bool channel_open_ = false;
    bool channel_requested_ = false;
    bool control_channel_requested_ = false;
    uint16_t video_stream_id_ = 0;
    uint16_t control_stream_id_ = 0;
    bool control_channel_open_ = false;
    int64_t last_move_at_us_ = 0;
    std::vector<uint8_t> pending_jpeg_;
    uint32_t pending_jpeg_sequence_ = 0;
    int64_t pending_jpeg_timestamp_us_ = 0;
    std::shared_ptr<DisplayControlAckTracker> control_acks_ =
        std::make_shared<DisplayControlAckTracker>();

    // Fixed-size observation only; protected by the existing service mutex.
    // No timing value is used for authorization, expiry or retry decisions.
    LoopDiagnostics loop_diagnostics_{};
    uint64_t timing_generation_ = 0;
    int64_t timing_next_log_us_ = 0;
    int64_t timing_next_slow_log_us_ = 0;
    int64_t timing_previous_log_us_ = 0;
    std::array<ControlTiming, 16> control_timings_{};
    size_t control_timing_count_ = 0;
    uint32_t control_timing_dropped_ = 0;

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
