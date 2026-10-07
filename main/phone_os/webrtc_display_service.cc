#include "phone_os/webrtc_display_service.h"

#include "phone_os/display_service.h"

#include <algorithm>
#include <cstring>
#include <inttypes.h>
#include <utility>

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "esp_peer_default.h"

namespace rodakos {
namespace {
constexpr const char* TAG = "WebRtcDisplay";
constexpr char kChannelLabel[] = "screen-jpeg";
constexpr uint8_t kChunkEnd = 0x80;
constexpr uint8_t kChunkSequenceMask = 0x7f;
constexpr size_t kChunkHeaderSize = 5;
constexpr uint16_t kDefaultChunkSize = 20000;
constexpr size_t kMaxChunkSize = 60000;
constexpr uint32_t kPeerLoopDelayMs = 10;
constexpr uint32_t kDataSendRetryDelayMs = 2;
// 视频通道是 latest-only：拥塞时尽快丢弃当前 JPEG，让下一张最新帧
// 重新竞争发送机会；控制通道仍保持可靠有序。
constexpr int64_t kDataSendRetryTimeoutUs = 20000;
constexpr int64_t kTransportStatsIntervalUs = 5000000;
constexpr int64_t kControlAckRetryTimeoutUs = 1000000;
constexpr uint32_t kControlAckMaxAttempts = 50;
constexpr int64_t kSlowPeerPhaseUs = 100000;
std::atomic<int64_t> next_control_retry_log_us{0};
std::atomic<int64_t> next_control_recovery_log_us{0};
bool AdmitControlDiagnostic(std::atomic<int64_t>& deadline, int64_t now_us) {
    auto next = deadline.load(std::memory_order_relaxed);
    return now_us >= next && deadline.compare_exchange_strong(
        next, now_us + kTransportStatsIntervalUs, std::memory_order_relaxed);
}
}

WebRtcDisplayService::WebRtcDisplayService(DisplayService* display_service)
    : display_service_(display_service) {}

WebRtcDisplayService::~WebRtcDisplayService() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        closing_ = true;
        task_retirement_owner_.Close();
    }
    Stop();
    task_retirement_owner_.Drain();
}

void WebRtcDisplayService::DurationStats::Observe(int64_t started_us, int64_t ended_us) {
    const uint64_t duration = static_cast<uint64_t>(std::max<int64_t>(0, ended_us - started_us));
    ++samples;
    total_us += duration;
    if (duration > maximum_us) {
        maximum_us = duration;
        maximum_at_us = started_us;
    }
}

WebRtcDisplayService::ScopedDuration::ScopedDuration(
    WebRtcDisplayService& service, DurationStats LoopDiagnostics::* observed)
    : owner(service), field(observed), started_us(esp_timer_get_time()) {
    std::lock_guard<std::recursive_mutex> lock(owner.mutex_);
    owner.loop_diagnostics_.service_wait.Observe(started_us, esp_timer_get_time());
    generation = owner.timing_generation_;
}

WebRtcDisplayService::ScopedDuration::~ScopedDuration() {
    owner.RecordDuration(field, started_us, esp_timer_get_time(), generation);
}

void WebRtcDisplayService::RecordDuration(DurationStats LoopDiagnostics::* field,
                                         int64_t started_us, int64_t ended_us, uint64_t generation) {
    const int64_t waiting_us = esp_timer_get_time();
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (generation != timing_generation_) return;
    loop_diagnostics_.service_wait.Observe(waiting_us, esp_timer_get_time());
    (loop_diagnostics_.*field).Observe(started_us, ended_us);
}

int WebRtcDisplayService::PumpPeer(esp_peer_handle_t peer) {
    uint64_t generation = 0;
    const int64_t service_wait_us = esp_timer_get_time();
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        loop_diagnostics_.service_wait.Observe(service_wait_us, esp_timer_get_time());
        generation = timing_generation_;
    }
    const int64_t waiting_us = esp_timer_get_time();
    int64_t acquired_us = 0;
    int64_t returned_us = 0;
    int result = ESP_PEER_ERR_NONE;
    {
        std::lock_guard<std::recursive_mutex> api_lock(peer_api_mutex_);
        acquired_us = esp_timer_get_time();
        result = esp_peer_main_loop(peer);
        returned_us = esp_timer_get_time();
    }
    RecordDuration(&LoopDiagnostics::api_wait, waiting_us, acquired_us, generation);
    RecordDuration(&LoopDiagnostics::sdk, acquired_us, returned_us, generation);
    return result;
}

bool WebRtcDisplayService::Start(const Config& config, SignalingCallback on_signaling,
                                StateCallback on_state, ControlCallback on_control) {
    if (display_service_ == nullptr || !on_signaling || config.width <= 0 || config.height <= 0 ||
        config.fps == 0 || config.fps > 30 || config.chunk_size == 0 ||
        !config.stream_lease || !config.stream_lease->IsActive()) {
        return false;
    }

    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (closing_ || running_ || peer_task_ != nullptr) {
        return false;
    }

    Config normalized = config;
    config_ = normalized;
    signaling_callback_ = std::move(on_signaling);
    state_callback_ = std::move(on_state);
    control_callback_ = std::move(on_control);
    stop_requested_ = false;
    terminal_state_ = ESP_PEER_STATE_CLOSED;
    channel_open_ = false;
    channel_requested_ = false;
    control_channel_requested_ = false;
    video_stream_id_ = 0;
    control_stream_id_ = 0;
    control_channel_open_ = false;
    last_move_at_us_ = 0;
    pending_jpeg_.clear();
    pending_jpeg_sequence_ = 0;
    pending_jpeg_timestamp_us_ = 0;
    control_acks_->Begin();
    peer_task_ready_ = false;
    stats_frames_received_.store(0, std::memory_order_relaxed);
    stats_frames_sent_.store(0, std::memory_order_relaxed);
    stats_frames_dropped_.store(0, std::memory_order_relaxed);
    stats_chunks_sent_.store(0, std::memory_order_relaxed);
    stats_bytes_sent_.store(0, std::memory_order_relaxed);
    stats_send_retries_.store(0, std::memory_order_relaxed);
    stats_send_would_block_.store(0, std::memory_order_relaxed);
    stats_send_errors_.store(0, std::memory_order_relaxed);
    stats_send_timeouts_.store(0, std::memory_order_relaxed);
    stats_send_duration_us_.store(0, std::memory_order_relaxed);
    stats_send_max_duration_us_.store(0, std::memory_order_relaxed);
    stats_api_lock_acquires_.store(0, std::memory_order_relaxed);
    stats_api_lock_wait_us_.store(0, std::memory_order_relaxed);
    stats_api_lock_max_wait_us_.store(0, std::memory_order_relaxed);
    stats_next_log_at_us_.store(esp_timer_get_time() + kTransportStatsIntervalUs,
                                std::memory_order_relaxed);
    loop_diagnostics_ = {};
    ++timing_generation_;
    timing_next_log_us_ = esp_timer_get_time() + kTransportStatsIntervalUs;
    timing_next_slow_log_us_ = 0;
    timing_previous_log_us_ = 0;
    control_timing_count_ = 0;
    control_timing_dropped_ = 0;

    if (!display_service_->StartCapture(normalized.width, normalized.height)) {
        signaling_callback_ = {};
        state_callback_ = {};
        control_callback_ = {};
        control_acks_->Close();
        config_.stream_lease.reset();
        return false;
    }

    auto default_cfg = MakeWebRtcPeerDefaultConfig();

    esp_peer_cfg_t peer_cfg{};
    peer_cfg.role = normalized.role;
    peer_cfg.video_info.codec = ESP_PEER_VIDEO_CODEC_MJPEG;
    peer_cfg.video_info.width = normalized.width;
    peer_cfg.video_info.height = normalized.height;
    peer_cfg.video_info.fps = normalized.fps;
    peer_cfg.enable_data_channel = true;
    peer_cfg.manual_ch_create = true;
    peer_cfg.no_auto_reconnect = true;
    peer_cfg.on_state = &WebRtcDisplayService::OnPeerState;
    peer_cfg.on_msg = &WebRtcDisplayService::OnPeerMessage;
    peer_cfg.on_data = &WebRtcDisplayService::OnPeerData;
    peer_cfg.on_channel_open = &WebRtcDisplayService::OnChannelOpen;
    peer_cfg.on_channel_close = &WebRtcDisplayService::OnChannelClose;
    peer_cfg.ctx = this;
    peer_cfg.extra_cfg = &default_cfg;
    peer_cfg.extra_size = sizeof(default_cfg);

    const esp_peer_ops_t* ops = esp_peer_get_default_impl();
    if (ops == nullptr) {
        ESP_LOGE(TAG, "Cannot start: esp_peer implementation is unavailable");
        display_service_->StopCapture();
        signaling_callback_ = {};
        state_callback_ = {};
        control_callback_ = {};
        control_acks_->Close();
        peer_ = nullptr;
        config_.stream_lease.reset();
        return false;
    }
    peer_resources_.Begin(TAG);
    const int open_ret = esp_peer_open(&peer_cfg, ops, &peer_);
    peer_resources_.Log(TAG, open_ret == ESP_PEER_ERR_NONE ? "open-after" : "open-failed", open_ret);
    if (open_ret != ESP_PEER_ERR_NONE) {
        ESP_LOGE(TAG, "esp_peer_open failed: %d internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
                 open_ret,
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        display_service_->StopCapture();
        signaling_callback_ = {};
        state_callback_ = {};
        control_callback_ = {};
        control_acks_->Close();
        peer_ = nullptr;
        config_.stream_lease.reset();
        peer_resources_.Log(TAG, "start-failed-cleanup", open_ret);
        return false;
    }

    running_ = true;
    const int connection_ret = esp_peer_new_connection(peer_);
    if (connection_ret != ESP_PEER_ERR_NONE) {
        ESP_LOGE(TAG, "esp_peer_new_connection failed: %d", connection_ret);
        running_ = false;
        esp_peer_close(peer_);
        peer_ = nullptr;
        display_service_->StopCapture();
        signaling_callback_ = {};
        state_callback_ = {};
        control_callback_ = {};
        control_acks_->Close();
        config_.stream_lease.reset();
        peer_resources_.Log(TAG, "start-failed-cleanup", connection_ret);
        return false;
    }

    // xTaskCreateWithCaps may schedule a task before it writes the output
    // handle. Publish the handle only after creation, and let PeerTask wait
    // for that publication before it can execute terminal cleanup.
    peer_task_ready_ = false;
    auto previous_retirement = std::move(peer_retirement_);
    peer_retirement_ = ReserveTaskRetirement(task_retirement_owner_, PeerTaskEntry, this);
    TaskHandle_t created_peer_task = nullptr;
    const BaseType_t task_ret = peer_retirement_ ? xTaskCreateWithCaps(
        TaskRetirementEntry, "webrtc_peer", 8192, TaskRetirementContext(peer_retirement_),
        4, &created_peer_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : pdFAIL;
    if (task_ret != pdPASS) {
        if (peer_retirement_) CancelTaskRetirement(peer_retirement_);
        peer_retirement_ = std::move(previous_retirement);
        ESP_LOGE(TAG, "Failed to create peer task: internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        running_ = false;
        esp_peer_close(peer_);
        peer_ = nullptr;
        display_service_->StopCapture();
        signaling_callback_ = {};
        state_callback_ = {};
        control_callback_ = {};
        control_acks_->Close();
        config_.stream_lease.reset();
        peer_resources_.Log(TAG, "start-failed-cleanup", ESP_PEER_ERR_NO_MEM);
        return false;
    }
    peer_task_ = created_peer_task;
    peer_task_ready_ = true;
    PublishTaskRetirement(peer_retirement_, created_peer_task);

    if (!display_service_->StartJpegStream(
            normalized.fps,
            [this](std::vector<uint8_t>&& jpeg, uint32_t sequence, int64_t timestamp_us) {
                HandleJpeg(std::move(jpeg), sequence, timestamp_us);
            })) {
        lock.unlock();
        ESP_LOGE(TAG, "Failed to start JPEG stream: %s", display_service_->last_error().c_str());
        Stop();
        return false;
    }
    return true;
}

void WebRtcDisplayService::Stop() {
    TaskRetirementTicket retirement;
    bool needs_cleanup = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        retirement = peer_retirement_;
        needs_cleanup = running_ || peer_ != nullptr;
        if (needs_cleanup && !stop_requested_) {
            terminal_state_ = ESP_PEER_STATE_CLOSED;
            stop_requested_ = true;
        }
        if (needs_cleanup) {
            if (config_.stream_lease) config_.stream_lease->Revoke();
            control_acks_->Close();
        }
    }
    if (retirement) {
        // Capture once: callback destruction may replace the logical session
        // while this caller is still waiting for its predecessor's stack.
        retirement.Join();
        return;
    }
    if (needs_cleanup) FinishStop();
}

void WebRtcDisplayService::RequestStop(esp_peer_state_t state) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!running_) {
        return;
    }
    if (!stop_requested_) {
        terminal_state_ = state;
        stop_requested_ = true;
    }
    if (config_.stream_lease) config_.stream_lease->Revoke();
    channel_open_ = false;
    control_acks_->Close();
}

void WebRtcDisplayService::FinishStop() {
    display_service_->StopJpegStream();
    esp_peer_handle_t peer = nullptr;
    std::vector<uint8_t> pending_jpeg_to_release;
    StateCallback callback;
    ControlCallback control_callback;
    esp_peer_state_t terminal_state = ESP_PEER_STATE_CLOSED;
    {
        std::lock_guard<std::recursive_mutex> api_lock(peer_api_mutex_);
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            peer = peer_;
            peer_ = nullptr;
            channel_open_ = false;
            channel_requested_ = false;
            control_channel_requested_ = false;
            video_stream_id_ = 0;
            control_stream_id_ = 0;
            control_channel_open_ = false;
            pending_jpeg_to_release.swap(pending_jpeg_);
            pending_jpeg_sequence_ = 0;
            pending_jpeg_timestamp_us_ = 0;
            control_acks_->Close();
            signaling_callback_ = {};
            callback = std::move(state_callback_);
            state_callback_ = {};
            control_callback = std::move(control_callback_);
            terminal_state = terminal_state_;
        }
        if (peer != nullptr) {
            esp_peer_close(peer);
        }
    }
    // Return the compact JPEG capacity before publishing task completion, with
    // neither the service mutex nor the peer API mutex held during deallocation.
    std::vector<uint8_t>().swap(pending_jpeg_to_release);
    display_service_->StopCapture();
    if (control_callback) {
        control_callback(std::string(), [](bool, const char*) {});
    }
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        running_ = false;
        config_.stream_lease.reset();
        // The retirement trampoline has not joined yet; this is native cleanup only.
        peer_resources_.Log(TAG, "stopped");
        // Publish only after resources are released. Keeping the service lock
        // prevents a new Start from racing its predecessor's terminal callback.
        if (callback) {
            callback(terminal_state);
        }
        peer_task_ = nullptr;
        peer_task_ready_ = false;
    }
}

bool WebRtcDisplayService::HandleRemoteMessage(esp_peer_msg_type_t type, const uint8_t* data,
                                              size_t size) {
    if (data == nullptr || size == 0 || (type != ESP_PEER_MSG_TYPE_SDP &&
                                         type != ESP_PEER_MSG_TYPE_CANDIDATE)) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> api_lock(peer_api_mutex_);
    esp_peer_handle_t peer = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (stop_requested_) {
            return false;
        }
        peer = peer_;
    }
    if (peer == nullptr) {
        return false;
    }
    esp_peer_msg_t message{};
    message.type = type;
    // esp_peer accepts SDP as NUL-terminated text; MQTT base64 decoding does not
    // provide a terminator. Keep message.size equal to the actual text length.
    std::vector<uint8_t> text(data, data + size);
    text.push_back(0);
    message.data = text.data();
    message.size = static_cast<int>(size);
    const int result = esp_peer_send_msg(peer, &message);
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        peer_resources_.Signal(TAG, type, result);
    }
    return result == ESP_PEER_ERR_NONE;
}

bool WebRtcDisplayService::IsRunning() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return running_;
}

bool WebRtcDisplayService::IsChannelOpen() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return channel_open_;
}

int WebRtcDisplayService::OnPeerState(esp_peer_state_t state, void* ctx) {
    auto* service = static_cast<WebRtcDisplayService*>(ctx);
    if (service != nullptr) {
        if (state == ESP_PEER_STATE_CLOSED || state == ESP_PEER_STATE_DISCONNECTED ||
            state == ESP_PEER_STATE_CONNECT_FAILED || state == ESP_PEER_STATE_DATA_CHANNEL_CLOSED ||
            state == ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED) {
            // These callbacks execute inside esp_peer; close only after its
            // main loop returns so no callback frees the active peer stack.
            service->RequestStop(state);
            return ESP_PEER_ERR_NONE;
        }
        if (state == ESP_PEER_STATE_DATA_CHANNEL_CONNECTED) {
            {
                std::lock_guard<std::recursive_mutex> lock(service->mutex_);
                service->peer_resources_.Log(TAG, "connected");
            }
            esp_peer_handle_t peer = nullptr;
            std::lock_guard<std::recursive_mutex> api_lock(service->peer_api_mutex_);
            {
                std::lock_guard<std::recursive_mutex> lock(service->mutex_);
                if (!service->stop_requested_ && !service->channel_requested_) {
                    service->channel_requested_ = true;
                    peer = service->peer_;
                }
            }
            if (peer != nullptr) {
                esp_peer_data_channel_cfg_t channel_cfg{};
                // Video is a latest-frame stream. Keep ordering for chunk
                // reassembly, but let packets expire quickly so a congested
                // Wi-Fi link cannot replay stale desktop frames seconds later.
                channel_cfg.type = ESP_PEER_DATA_CHANNEL_PARTIAL_RELIABLE_TIMEOUT;
                channel_cfg.ordered = true;
                channel_cfg.max_packet_lifetime = 250;
                channel_cfg.label = const_cast<char*>(kChannelLabel);
                if (esp_peer_create_data_channel(peer, &channel_cfg) != ESP_PEER_ERR_NONE) {
                    service->RequestStop(ESP_PEER_STATE_CONNECT_FAILED);
                    return ESP_PEER_ERR_NONE;
                }
                esp_peer_data_channel_cfg_t control_cfg{};
                control_cfg.type = ESP_PEER_DATA_CHANNEL_RELIABLE;
                control_cfg.ordered = true;
                control_cfg.label = const_cast<char*>("screen-control");
                service->control_channel_requested_ = true;
                if (esp_peer_create_data_channel(peer, &control_cfg) != ESP_PEER_ERR_NONE) {
                    service->RequestStop(ESP_PEER_STATE_CONNECT_FAILED);
                    return ESP_PEER_ERR_NONE;
                }
            }
        }
        service->EmitState(state);
    }
    return ESP_PEER_ERR_NONE;
}

int WebRtcDisplayService::OnPeerMessage(esp_peer_msg_t* message, void* ctx) {
    auto* service = static_cast<WebRtcDisplayService*>(ctx);
    if (service != nullptr && message != nullptr) {
        service->EmitMessage(message);
    }
    return ESP_PEER_ERR_NONE;
}

int WebRtcDisplayService::OnPeerData(esp_peer_data_frame_t* frame, void* ctx) {
    const int64_t entered_us = esp_timer_get_time();
    auto* service = static_cast<WebRtcDisplayService*>(ctx);
    if (service != nullptr && frame != nullptr) service->HandleControlData(frame, entered_us);
    return ESP_PEER_ERR_NONE;
}

void WebRtcDisplayService::HandleControlData(esp_peer_data_frame_t* frame, int64_t entered_us) {
    if (frame == nullptr || frame->data == nullptr || frame->size <= 0 || frame->size > 2048) {
        return;
    }
    ControlCallback callback;
    DisplayControlAckTracker::InstancePtr instance;
    uint64_t timing_generation = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (stop_requested_ || frame->stream_id != control_stream_id_) return;
        loop_diagnostics_.service_wait.Observe(entered_us, esp_timer_get_time());
        callback = control_callback_;
        instance = control_acks_->Current();
        timing_generation = timing_generation_;
    }
    if (!callback) return;
    const std::string payload(reinterpret_cast<const char*>(frame->data),
                              static_cast<size_t>(frame->size));
    cJSON* root = cJSON_ParseWithLength(payload.data(), payload.size());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        QueueControlAck(instance, 0, false, "invalid_json");
        return;
    }
    const cJSON* seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
    const bool valid_sequence = cJSON_IsNumber(seq) && seq->valuedouble >= 1.0 &&
                                seq->valuedouble <= 4294967295.0 &&
                                seq->valuedouble == static_cast<double>(static_cast<uint32_t>(seq->valuedouble));
    const uint32_t sequence = valid_sequence ? static_cast<uint32_t>(seq->valuedouble) : 0;
    const bool sequence_ok = valid_sequence && control_acks_->AdmitSequence(instance, sequence);
    if (!sequence_ok) {
        cJSON_Delete(root);
        QueueControlAck(instance, sequence, false, "invalid_or_replayed_sequence");
        return;
    }
    const cJSON* kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
    const std::string kind_name = cJSON_IsString(kind) ? kind->valuestring : "";
    bool is_move = false;
    if (kind_name == "pointer") {
        const cJSON* action = cJSON_GetObjectItemCaseSensitive(root, "action");
        const cJSON* x = cJSON_GetObjectItemCaseSensitive(root, "x");
        const cJSON* y = cJSON_GetObjectItemCaseSensitive(root, "y");
        const bool coords_ok = cJSON_IsNumber(x) && cJSON_IsNumber(y) &&
                               x->valuedouble >= 0.0 && x->valuedouble < 320.0 &&
                               y->valuedouble >= 0.0 && y->valuedouble < 240.0 &&
                               x->valuedouble == static_cast<double>(x->valueint) &&
                               y->valuedouble == static_cast<double>(y->valueint);
        if (!coords_ok) {
            cJSON_Delete(root);
            QueueControlAck(instance, sequence, false, "coordinates_out_of_range");
            return;
        }
        if (cJSON_IsString(action) && std::strcmp(action->valuestring, "move") == 0) {
            is_move = true;
            const int64_t now = esp_timer_get_time();
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (!control_acks_->IsCurrent(instance)) { cJSON_Delete(root); return; }
            if (last_move_at_us_ != 0 && now - last_move_at_us_ < 20000) {
                cJSON_Delete(root);
                QueueControlAck(instance, sequence, false, "pointer_rate_limited");
                return;
            }
            last_move_at_us_ = now;
        }
    }
    cJSON_Delete(root);
    const uint16_t stream_id = frame->stream_id;
    const int64_t dispatch_us = esp_timer_get_time();
    callback(payload, control_acks_->MakeReply(instance, sequence, kind_name));
    const int64_t returned_us = esp_timer_get_time();
    if (!is_move) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (timing_generation != timing_generation_) return;
        if (control_timing_count_ < control_timings_.size()) {
            control_timings_[control_timing_count_++] = {
                timing_generation, sequence, stream_id, entered_us, dispatch_us, returned_us};
        } else ++control_timing_dropped_;
    }
}

bool WebRtcDisplayService::SendControlAck(const DisplayControlAckTracker::Ack& ack, int* error) {
    if (error) *error = ESP_PEER_ERR_WRONG_STATE;
    std::lock_guard<std::recursive_mutex> api_lock(peer_api_mutex_);
    esp_peer_handle_t peer = nullptr;
    uint16_t stream_id = 0;
    StreamLeasePtr lease;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (stop_requested_ || peer_ == nullptr || !control_channel_open_ ||
            !control_acks_->IsCurrent(ack.instance)) return false;
        peer = peer_;
        stream_id = control_stream_id_;
        lease = config_.stream_lease;
    }
    cJSON* response_json = cJSON_CreateObject();
    const bool complete = response_json && cJSON_AddNumberToObject(response_json, "version", 1) &&
        cJSON_AddNumberToObject(response_json, "seq", ack.sequence) &&
        cJSON_AddBoolToObject(response_json, "accepted", ack.accepted) &&
        (ack.reason.empty() || cJSON_AddStringToObject(response_json, "reason", ack.reason.c_str()));
    char* encoded = complete ? cJSON_PrintUnformatted(response_json) : nullptr;
    cJSON_Delete(response_json);
    if (!encoded) {
        if (error) *error = ESP_PEER_ERR_NO_MEM;
        return false;
    }
    esp_peer_data_frame_t response{};
    response.type = ESP_PEER_DATA_CHANNEL_DATA;
    response.stream_id = stream_id;
    response.data = reinterpret_cast<uint8_t*>(encoded);
    response.size = static_cast<int>(std::strlen(encoded));
    int result = ESP_PEER_ERR_WRONG_STATE;
    bool current = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        current = !stop_requested_ && peer_ == peer && control_channel_open_ &&
            control_stream_id_ == stream_id && config_.stream_lease == lease &&
            control_acks_->IsCurrent(ack.instance);
    }
    const bool admitted = current && lease && lease->TryApply([&] {
        result = esp_peer_send_data(peer, &response);
    });
    cJSON_free(encoded);
    if (!admitted) RequestStop(ESP_PEER_STATE_DATA_CHANNEL_CLOSED);
    if (error) *error = result;
    return result == ESP_PEER_ERR_NONE;
}

int WebRtcDisplayService::OnChannelOpen(esp_peer_data_channel_info_t* channel, void* ctx) {
    auto* service = static_cast<WebRtcDisplayService*>(ctx);
    if (service == nullptr || channel == nullptr) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(service->mutex_);
        if (!service->stop_requested_ && channel->label != nullptr) {
            if (std::strcmp(channel->label, kChannelLabel) == 0) {
                service->video_stream_id_ = channel->stream_id;
                service->channel_open_ = true;
            } else if (std::strcmp(channel->label, "screen-control") == 0) {
                service->control_stream_id_ = channel->stream_id;
                service->control_channel_open_ = true;
            }
        }
    }
    return ESP_PEER_ERR_NONE;
}

int WebRtcDisplayService::OnChannelClose(esp_peer_data_channel_info_t* channel, void* ctx) {
    auto* service = static_cast<WebRtcDisplayService*>(ctx);
    if (service != nullptr) {
        service->RequestStop(ESP_PEER_STATE_DATA_CHANNEL_CLOSED);
    }
    return ESP_PEER_ERR_NONE;
}

void WebRtcDisplayService::PeerTaskEntry(void* arg) {
    auto* service = static_cast<WebRtcDisplayService*>(arg);
    if (service != nullptr) {
        service->PeerTask();
    }
}

void WebRtcDisplayService::PeerTask() {
    // xTaskCreateWithCaps can start the task before returning its handle to
    // Start. Park here until Start publishes that handle so a fast terminal
    // callback cannot race task-handle publication.
    while (true) {
        bool ready = false;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            ready = peer_task_ready_;
        }
        if (ready) {
            break;
        }
        vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(1)));
    }

    int64_t previous_loop_us = -1;
    while (true) {
        const int64_t loop_us = esp_timer_get_time();
        esp_peer_handle_t peer = nullptr;
        uint64_t timing_generation = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            loop_diagnostics_.service_wait.Observe(loop_us, esp_timer_get_time());
            if (stop_requested_ || peer_ == nullptr) {
                break;
            }
            peer = peer_;
            timing_generation = timing_generation_;
        }
        if (previous_loop_us >= 0)
            RecordDuration(&LoopDiagnostics::gap, previous_loop_us, loop_us, timing_generation);
        previous_loop_us = loop_us;
        // SDK main_loop, signalling, data sends and close retain one API lock.
        const int ret = PumpPeer(peer);
        FlushControlAcks();
        // JPEG 编码回调只替换 pending latest frame；在 peer task 中发送，
        // 避免 JPEG worker 与 main_loop 争用 peer_api_mutex_。控制 ACK 仍
        // 在同一轮 flush，保证控制通道不会被画面发送线程阻塞。
        std::vector<uint8_t> pending_jpeg;
        uint32_t pending_sequence = 0;
        int64_t pending_timestamp_us = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (!stop_requested_ && !control_acks_->HasPending() && channel_open_ && !pending_jpeg_.empty()) {
                pending_jpeg.swap(pending_jpeg_);
                pending_sequence = pending_jpeg_sequence_;
                pending_timestamp_us = pending_jpeg_timestamp_us_;
                pending_jpeg_sequence_ = 0;
                pending_jpeg_timestamp_us_ = 0;
            }
        }
        if (!pending_jpeg.empty()) {
            SendJpegChunks(pending_jpeg);
            (void)pending_sequence;
            (void)pending_timestamp_us;
            FlushControlAcks();
        }
        MaybeLogTransportStats();
        MaybeLogTimings();
        if (ret != ESP_PEER_ERR_NONE && ret != ESP_PEER_ERR_WOULD_BLOCK) {
            ESP_LOGW(TAG, "esp_peer_main_loop failed: %d", ret);
            RequestStop(ESP_PEER_STATE_CONNECT_FAILED);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(kPeerLoopDelayMs));
    }
    MaybeLogTimings(true);
    FinishStop();
}

void WebRtcDisplayService::QueueControlAck(const DisplayControlAckTracker::InstancePtr& instance,
                                          uint32_t sequence, bool accepted, const char* reason) {
    control_acks_->Queue(instance, sequence, accepted, reason);
}

void WebRtcDisplayService::FlushControlAcks() {
    ScopedDuration timing{*this, &LoopDiagnostics::ack};
    // Only ACKs are retried, never inputs. Keep FIFO ownership until SCTP accepts
    // the head; the next PeerTask loop pumps receive/close before trying again.
    std::lock_guard<std::recursive_mutex> api_lock(peer_api_mutex_);
    if (control_acks_->Overflowed()) {
        ESP_LOGW(TAG, "control ACK queue unavailable; closing peer");
        RequestStop(ESP_PEER_STATE_DATA_CHANNEL_CLOSED);
        return;
    }
    DisplayControlAckTracker::Ack ack;
    bool no_memory = false;
    for (size_t count = 0; count < 32 && control_acks_->Front(ack, no_memory); ++count) {
        const int64_t now_us = esp_timer_get_time();
        if (ack.attempts >= kControlAckMaxAttempts ||
            (ack.first_attempt_us >= 0 && now_us - ack.first_attempt_us >= kControlAckRetryTimeoutUs)) {
            ESP_LOGW(TAG, "control ACK retry exhausted: seq=%u attempts=%u; closing peer",
                     static_cast<unsigned>(ack.sequence), static_cast<unsigned>(ack.attempts));
            RequestStop(ESP_PEER_STATE_DATA_CHANNEL_CLOSED);
            return;
        }
        control_acks_->MarkAttempt(ack, now_us);
        int error = no_memory ? ESP_PEER_ERR_NO_MEM : ESP_PEER_ERR_NONE;
        if (!no_memory && SendControlAck(ack, &error)) {
            if (ack.attempts != 0 && AdmitControlDiagnostic(next_control_recovery_log_us, now_us)) {
                ESP_LOGI(TAG, "control ACK recovered: seq=%u attempts=%u elapsed_ms=%u",
                         static_cast<unsigned>(ack.sequence), static_cast<unsigned>(ack.attempts + 1),
                         static_cast<unsigned>((now_us - ack.first_attempt_us) / 1000));
            }
            control_acks_->Complete(ack);
            continue;
        }
        if (!control_acks_->IsCurrent(ack.instance)) return;
        if (error == ESP_PEER_ERR_WOULD_BLOCK || error == ESP_PEER_ERR_NO_MEM) {
            if (ack.attempts == 0 && AdmitControlDiagnostic(next_control_retry_log_us, now_us))
                ESP_LOGW(TAG, "control ACK retry: seq=%u error=%d",
                         static_cast<unsigned>(ack.sequence), error);
            return;
        }
        ESP_LOGW(TAG, "control ACK failed: seq=%u error=%d; closing peer",
                 static_cast<unsigned>(ack.sequence), error);
        RequestStop(ESP_PEER_STATE_DATA_CHANNEL_CLOSED);
        return;
    }
}

void WebRtcDisplayService::HandleJpeg(std::vector<uint8_t>&& jpeg, uint32_t sequence,
                                     int64_t timestamp_us) {
    if (jpeg.empty()) {
        return;
    }
    stats_frames_received_.fetch_add(1, std::memory_order_relaxed);
    bool accepted = false;
    bool expired = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        // Initial capture may precede DCEP; the peer task waits for video open.
        if (running_ && !stop_requested_) {
            const auto& lease = config_.stream_lease;
            accepted = lease && lease->TryApply([&] {
                if (!pending_jpeg_.empty()) {
                    // Replace the previous pending JPEG rather than queue it.
                    stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
                }
                pending_jpeg_ = std::move(jpeg);
                pending_jpeg_sequence_ = sequence;
                pending_jpeg_timestamp_us_ = timestamp_us;
            });
            expired = !accepted;
        }
    }
    if (expired) RequestStop(ESP_PEER_STATE_DATA_CHANNEL_CLOSED);
    if (!accepted) {
        stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
        MaybeLogTransportStats();
    }
}

bool WebRtcDisplayService::SendJpegChunks(const std::vector<uint8_t>& jpeg) {
    ScopedDuration timing{*this, &LoopDiagnostics::jpeg};
    const int64_t send_started_at_us = esp_timer_get_time();
    esp_peer_handle_t peer = nullptr;
    StreamLeasePtr lease;
    uint16_t stream_id = 0;
    size_t chunk_size = kDefaultChunkSize;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (stop_requested_ || !channel_open_) {
            stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
            MaybeLogTransportStats();
            return false;
        }
        peer = peer_;
        lease = config_.stream_lease;
        stream_id = video_stream_id_;
        chunk_size = config_.chunk_size == 0
                         ? kDefaultChunkSize
                         : std::min<size_t>(config_.chunk_size, kMaxChunkSize);
    }
    if (peer == nullptr || jpeg.size() == 0) {
        stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
        MaybeLogTransportStats();
        return false;
    }

    const size_t chunk_count = (jpeg.size() + chunk_size - 1) / chunk_size;
    if (chunk_count == 0 || chunk_count > (kChunkSequenceMask + 1u)) {
        stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
        stats_send_errors_.fetch_add(1, std::memory_order_relaxed);
        MaybeLogTransportStats();
        return false;
    }
    std::vector<uint8_t> packet(kChunkHeaderSize + chunk_size);
    size_t offset = 0;
    for (size_t sequence = 0; offset < jpeg.size(); ++sequence) {
        const size_t payload_size = std::min<size_t>(chunk_size, jpeg.size() - offset);
        packet[0] = static_cast<uint8_t>(sequence) |
                    (offset + payload_size == jpeg.size() ? kChunkEnd : 0);
        packet[1] = static_cast<uint8_t>((payload_size >> 24) & 0xff);
        packet[2] = static_cast<uint8_t>((payload_size >> 16) & 0xff);
        packet[3] = static_cast<uint8_t>((payload_size >> 8) & 0xff);
        packet[4] = static_cast<uint8_t>(payload_size & 0xff);
        std::memcpy(packet.data() + kChunkHeaderSize, jpeg.data() + offset, payload_size);
        esp_peer_data_frame_t frame{};
        frame.type = ESP_PEER_DATA_CHANNEL_DATA;
        frame.data = packet.data();
        frame.size = static_cast<int>(kChunkHeaderSize + payload_size);
        // 有序 DataChannel 在发送缓存排空期间可能短暂返回 WOULD_BLOCK。
        // 原地重试同一分片，避免从序号 0 重启半帧并与队列中的分片交错。
        // 设置截止时间，拥塞时让 latest-only 流及时放弃并恢复，而不是无限阻塞。
        const int64_t retry_deadline = esp_timer_get_time() + kDataSendRetryTimeoutUs;
        while (true) {
            int ret = ESP_PEER_ERR_WRONG_STATE;
            {
                // 等待容量时不要持有 peer_api_mutex_，否则 PeerTask 无法进入
                // esp_peer_main_loop 排空传输缓存。
                const int64_t lock_wait_started_us = esp_timer_get_time();
                std::unique_lock<std::recursive_mutex> api_lock(peer_api_mutex_);
                const uint64_t lock_wait_us = static_cast<uint64_t>(std::max<int64_t>(
                    0, esp_timer_get_time() - lock_wait_started_us));
                stats_api_lock_acquires_.fetch_add(1, std::memory_order_relaxed);
                stats_api_lock_wait_us_.fetch_add(lock_wait_us, std::memory_order_relaxed);
                uint64_t previous_lock_max =
                    stats_api_lock_max_wait_us_.load(std::memory_order_relaxed);
                while (previous_lock_max < lock_wait_us &&
                       !stats_api_lock_max_wait_us_.compare_exchange_weak(
                           previous_lock_max, lock_wait_us, std::memory_order_relaxed,
                           std::memory_order_relaxed)) {
                }
                {
                    std::lock_guard<std::recursive_mutex> lock(mutex_);
                    if (stop_requested_ || !channel_open_ || peer_ == nullptr ||
                        peer_ != peer || video_stream_id_ != stream_id || config_.stream_lease != lease) {
                        stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
                        MaybeLogTransportStats();
                        return false;
                    }
                    frame.stream_id = stream_id;
                }
                if (!lease || !lease->TryApply([&] { ret = esp_peer_send_data(peer, &frame); })) {
                    stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
                    RequestStop(ESP_PEER_STATE_DATA_CHANNEL_CLOSED);
                    return false;
                }
            }
            if (ret == ESP_PEER_ERR_NONE) {
                stats_chunks_sent_.fetch_add(1, std::memory_order_relaxed);
                stats_bytes_sent_.fetch_add(payload_size, std::memory_order_relaxed);
                break;
            }
            if (ret != ESP_PEER_ERR_WOULD_BLOCK || esp_timer_get_time() >= retry_deadline) {
                stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
                if (ret == ESP_PEER_ERR_WOULD_BLOCK) {
                    stats_send_timeouts_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    stats_send_errors_.fetch_add(1, std::memory_order_relaxed);
                }
                MaybeLogTransportStats();
                return false;
            }
            stats_send_would_block_.fetch_add(1, std::memory_order_relaxed);
            stats_send_retries_.fetch_add(1, std::memory_order_relaxed);
            // CONFIG_FREERTOS_HZ 可能为 100，此时 pdMS_TO_TICKS(2) 为 0；
            // 等待 PeerTask 排空 SCTP 缓存时至少让出一个调度 tick。
            vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(kDataSendRetryDelayMs)));
            // SendJpegChunks 现在由 PeerTask 执行；若底层仍报告缓存满，
            // 在同一发送状态机内主动跑一次 main_loop，避免等待 SCTP
            // 排空时只重试 send_data 而没有任何泵浦机会。
            const int loop_ret = PumpPeer(peer);
            if (loop_ret != ESP_PEER_ERR_NONE && loop_ret != ESP_PEER_ERR_WOULD_BLOCK) {
                stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
                stats_send_errors_.fetch_add(1, std::memory_order_relaxed);
                MaybeLogTransportStats();
                return false;
            }
        }
        offset += payload_size;

        // 让可靠控制通道在视频分片之间获得调度机会。单片 JPEG 无需在
        // 发送完成后再次泵浦；外层 PeerTask 会立即进入下一轮 main_loop，
        // 避免每帧额外一次 agent/SCTP 往返。跨多个分片时仍在分片之间
        // 泵浦，确保 pointer down/up 不被整帧发送阻塞。
        if (offset < jpeg.size()) {
            const int loop_ret = PumpPeer(peer);
            FlushControlAcks();
            if (control_acks_->HasPending()) {
                // The partially sent latest-only frame may expire. Do not add
                // video pressure while a reliable result is waiting for space.
                stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            if (loop_ret != ESP_PEER_ERR_NONE && loop_ret != ESP_PEER_ERR_WOULD_BLOCK) {
                stats_frames_dropped_.fetch_add(1, std::memory_order_relaxed);
                stats_send_errors_.fetch_add(1, std::memory_order_relaxed);
                MaybeLogTransportStats();
                return false;
            }
        }
    }
    const uint64_t duration_us = static_cast<uint64_t>(
        std::max<int64_t>(0, esp_timer_get_time() - send_started_at_us));
    stats_frames_sent_.fetch_add(1, std::memory_order_relaxed);
    stats_send_duration_us_.fetch_add(duration_us, std::memory_order_relaxed);
    uint64_t previous_max = stats_send_max_duration_us_.load(std::memory_order_relaxed);
    while (previous_max < duration_us &&
           !stats_send_max_duration_us_.compare_exchange_weak(
               previous_max, duration_us, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
    MaybeLogTransportStats();
    return true;
}

void WebRtcDisplayService::MaybeLogTimings(bool force) {
    const int64_t now_us = esp_timer_get_time();
    LoopDiagnostics stats;
    std::array<ControlTiming, 16> controls;
    size_t count = 0;
    uint32_t dropped = 0;
    uint64_t generation = 0;
    bool periodic = false;
    int64_t previous_log_us = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        loop_diagnostics_.service_wait.Observe(now_us, esp_timer_get_time());
        periodic = force || now_us >= timing_next_log_us_;
        const uint64_t maximum = std::max({loop_diagnostics_.gap.maximum_us,
            loop_diagnostics_.service_wait.maximum_us,
            loop_diagnostics_.api_wait.maximum_us, loop_diagnostics_.sdk.maximum_us,
            loop_diagnostics_.ack.maximum_us, loop_diagnostics_.jpeg.maximum_us});
        if (!periodic && (maximum < kSlowPeerPhaseUs || now_us < timing_next_slow_log_us_)) return;
        stats = loop_diagnostics_;
        generation = timing_generation_;
        previous_log_us = timing_previous_log_us_;
        timing_next_slow_log_us_ = now_us + kTransportStatsIntervalUs;
        if (periodic) {
            controls = control_timings_;
            count = control_timing_count_;
            dropped = control_timing_dropped_;
            control_timing_count_ = 0;
            control_timing_dropped_ = 0;
            loop_diagnostics_ = {};
            timing_next_log_us_ = now_us + kTransportStatsIntervalUs;
        }
    }
    const int64_t log_started_us = esp_timer_get_time();
    const char* window = force ? "final" : periodic ? "periodic" : "slow";
    ESP_LOGI(TAG, "peer timing: gen=%" PRIu64 " window=%s at_us=%" PRId64
        " gaps=%" PRIu64 " gap_max_us=%" PRIu64 " gap_at_us=%" PRId64
        " service_max_us=%" PRIu64 " service_at_us=%" PRId64
        " pump_calls=%" PRIu64 " api_max_us=%" PRIu64 " api_at_us=%" PRId64
        " sdk_max_us=%" PRIu64 " sdk_at_us=%" PRId64,
        generation, window, now_us, stats.gap.samples, stats.gap.maximum_us, stats.gap.maximum_at_us,
        stats.service_wait.maximum_us, stats.service_wait.maximum_at_us,
        stats.sdk.samples, stats.api_wait.maximum_us, stats.api_wait.maximum_at_us,
        stats.sdk.maximum_us, stats.sdk.maximum_at_us);
    ESP_LOGI(TAG, "peer phases: gen=%" PRIu64 " window=%s"
        " ack_calls=%" PRIu64 " ack_max_us=%" PRIu64 " ack_at_us=%" PRId64
        " jpeg_calls=%" PRIu64 " jpeg_max_us=%" PRIu64 " jpeg_at_us=%" PRId64
        " prior_log_us=%" PRId64 " control_samples=%u control_dropped=%u",
        generation, window, stats.ack.samples, stats.ack.maximum_us, stats.ack.maximum_at_us,
        stats.jpeg.samples, stats.jpeg.maximum_us, stats.jpeg.maximum_at_us, previous_log_us,
        static_cast<unsigned>(count), static_cast<unsigned>(dropped));
    // Buffered device timestamps are captured at their stages, not at this
    // later log flush. SDK/ACK phases nested in JPEG are inclusive, not additive.
    for (size_t i = 0; i < count; ++i) {
        const auto& sample = controls[i];
        ESP_LOGI(TAG, "control timing: gen=%" PRIu64 " seq=%u stream=%u"
            " entered_us=%" PRId64 " dispatch_us=%" PRId64 " returned_us=%" PRId64,
            sample.generation, static_cast<unsigned>(sample.sequence),
            static_cast<unsigned>(sample.stream_id), sample.entered_us, sample.dispatch_us, sample.returned_us);
    }
    const int64_t log_finished_us = esp_timer_get_time();
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (generation == timing_generation_) timing_previous_log_us_ = log_finished_us - log_started_us;
}

void WebRtcDisplayService::MaybeLogTransportStats() {
    const int64_t now_us = esp_timer_get_time();
    int64_t next_log_at_us = stats_next_log_at_us_.load(std::memory_order_relaxed);
    while (next_log_at_us <= now_us) {
        if (!stats_next_log_at_us_.compare_exchange_weak(
                next_log_at_us, now_us + kTransportStatsIntervalUs,
                std::memory_order_relaxed, std::memory_order_relaxed)) {
            continue;
        }
        // 交换成窗口计数，日志反映最近 5 秒，而不是从 session 启动开始的
        // 累计值；交换是原子的，JPEG worker 可在日志输出期间继续发送。
        const uint64_t frames_received =
            stats_frames_received_.exchange(0, std::memory_order_relaxed);
        const uint64_t frames_sent = stats_frames_sent_.exchange(0, std::memory_order_relaxed);
        const uint64_t frames_dropped =
            stats_frames_dropped_.exchange(0, std::memory_order_relaxed);
        const uint64_t chunks_sent = stats_chunks_sent_.exchange(0, std::memory_order_relaxed);
        const uint64_t bytes_sent = stats_bytes_sent_.exchange(0, std::memory_order_relaxed);
        const uint64_t retries = stats_send_retries_.exchange(0, std::memory_order_relaxed);
        const uint64_t would_block =
            stats_send_would_block_.exchange(0, std::memory_order_relaxed);
        const uint64_t errors = stats_send_errors_.exchange(0, std::memory_order_relaxed);
        const uint64_t timeouts = stats_send_timeouts_.exchange(0, std::memory_order_relaxed);
        const uint64_t total_duration_us =
            stats_send_duration_us_.exchange(0, std::memory_order_relaxed);
        const uint64_t max_duration_us =
            stats_send_max_duration_us_.exchange(0, std::memory_order_relaxed);
        const uint64_t api_lock_acquires =
            stats_api_lock_acquires_.exchange(0, std::memory_order_relaxed);
        const uint64_t api_lock_wait_us =
            stats_api_lock_wait_us_.exchange(0, std::memory_order_relaxed);
        const uint64_t api_lock_max_wait_us =
            stats_api_lock_max_wait_us_.exchange(0, std::memory_order_relaxed);
        const uint64_t avg_duration_us =
            frames_sent == 0 ? 0 : total_duration_us / frames_sent;
        const uint64_t avg_api_lock_wait_us =
            api_lock_acquires == 0 ? 0 : api_lock_wait_us / api_lock_acquires;
        ESP_LOGI(TAG,
                 "screen transport 5s: rx=%u sent=%u drop=%u chunks=%u bytes=%u "
                 "retry=%u would_block=%u timeout=%u error=%u avg_send_ms=%u max_send_ms=%u "
                 "avg_lock_ms=%u max_lock_ms=%u",
                 static_cast<unsigned>(frames_received), static_cast<unsigned>(frames_sent),
                 static_cast<unsigned>(frames_dropped), static_cast<unsigned>(chunks_sent),
                 static_cast<unsigned>(bytes_sent), static_cast<unsigned>(retries),
                 static_cast<unsigned>(would_block), static_cast<unsigned>(timeouts),
                 static_cast<unsigned>(errors), static_cast<unsigned>(avg_duration_us / 1000),
                 static_cast<unsigned>(max_duration_us / 1000),
                 static_cast<unsigned>(avg_api_lock_wait_us / 1000),
                 static_cast<unsigned>(api_lock_max_wait_us / 1000));
        return;
    }
}

void WebRtcDisplayService::EmitState(esp_peer_state_t state) {
    StateCallback callback;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        callback = state_callback_;
    }
    if (callback) {
        callback(state);
    }
}

void WebRtcDisplayService::EmitMessage(esp_peer_msg_t* message) {
    SignalingCallback callback;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        callback = signaling_callback_;
    }
    if (!callback || message == nullptr || message->data == nullptr || message->size <= 0) {
        return;
    }
    std::vector<uint8_t> payload(message->data, message->data + message->size);
    callback(message->type, std::move(payload));
}

}  // namespace rodakos
