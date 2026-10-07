#include "phone_os/webrtc_camera_service.h"

#include "phone_os/camera_service.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "esp_peer_default.h"

namespace rodakos {
namespace {
constexpr const char* TAG = "WebRtcCamera";
constexpr char kChannelLabel[] = "camera-jpeg";
constexpr uint8_t kChunkEnd = 0x80;
constexpr uint8_t kChunkSequenceMask = 0x7f;
constexpr size_t kChunkHeaderSize = 5;
constexpr uint16_t kDefaultChunkSize = 10000;
constexpr size_t kMaxChunkSize = 60000;
constexpr uint32_t kPeerLoopDelayMs = 10;
constexpr uint32_t kDataSendRetryDelayMs = 2;
constexpr int64_t kDataSendRetryTimeoutUs = 100000;
}

WebRtcCameraService::WebRtcCameraService(CameraService* camera_service)
    : camera_service_(camera_service) {}

WebRtcCameraService::~WebRtcCameraService() { Stop(); }

bool WebRtcCameraService::Start(const Config& config, SignalingCallback on_signaling,
                                StateCallback on_state) {
    if (camera_service_ == nullptr || !on_signaling || config.width <= 0 || config.height <= 0 ||
        config.fps == 0 || config.fps > 30 || config.chunk_size == 0) {
        return false;
    }

    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (running_ || peer_task_ != nullptr) {
        return false;
    }

    Config normalized = config;
    config_ = normalized;
    signaling_callback_ = std::move(on_signaling);
    state_callback_ = std::move(on_state);
    stop_requested_ = false;
    terminal_state_ = ESP_PEER_STATE_CLOSED;
    channel_open_ = false;
    channel_requested_ = false;
    peer_task_ready_ = false;

    if (!camera_service_->StartPreview(CameraService::PreviewOwner::kRemote,
                                       normalized.width, normalized.height)) {
        signaling_callback_ = {};
        state_callback_ = {};
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
    peer_cfg.on_state = &WebRtcCameraService::OnPeerState;
    peer_cfg.on_msg = &WebRtcCameraService::OnPeerMessage;
    peer_cfg.on_channel_open = &WebRtcCameraService::OnChannelOpen;
    peer_cfg.on_channel_close = &WebRtcCameraService::OnChannelClose;
    peer_cfg.ctx = this;
    peer_cfg.extra_cfg = &default_cfg;
    peer_cfg.extra_size = sizeof(default_cfg);

    const esp_peer_ops_t* ops = esp_peer_get_default_impl();
    if (ops == nullptr) {
        ESP_LOGE(TAG, "Cannot start: esp_peer implementation is unavailable");
        camera_service_->StopPreview(CameraService::PreviewOwner::kRemote);
        signaling_callback_ = {};
        state_callback_ = {};
        peer_ = nullptr;
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
        camera_service_->StopPreview(CameraService::PreviewOwner::kRemote);
        signaling_callback_ = {};
        state_callback_ = {};
        peer_ = nullptr;
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
        camera_service_->StopPreview(CameraService::PreviewOwner::kRemote);
        signaling_callback_ = {};
        state_callback_ = {};
        peer_resources_.Log(TAG, "start-failed-cleanup", connection_ret);
        return false;
    }

    // xTaskCreateWithCaps may schedule a task before it writes the output
    // handle. Publish the handle only after creation, and let PeerTask wait
    // for that publication before it can execute terminal cleanup.
    peer_task_ready_ = false;
    TaskHandle_t created_peer_task = nullptr;
    const BaseType_t task_ret = xTaskCreateWithCaps(
        PeerTaskEntry, "webrtc_peer", 8192, this, 4, &created_peer_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create peer task: internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        running_ = false;
        esp_peer_close(peer_);
        peer_ = nullptr;
        camera_service_->StopPreview(CameraService::PreviewOwner::kRemote);
        signaling_callback_ = {};
        state_callback_ = {};
        peer_resources_.Log(TAG, "start-failed-cleanup", ESP_PEER_ERR_NO_MEM);
        return false;
    }
    peer_task_ = created_peer_task;
    peer_task_ready_ = true;

    if (!camera_service_->StartJpegStream(
            normalized.fps,
            [this](std::vector<uint8_t>&& jpeg, uint32_t sequence, int64_t timestamp_us) {
                HandleJpeg(std::move(jpeg), sequence, timestamp_us);
            })) {
        lock.unlock();
        ESP_LOGE(TAG, "Failed to start JPEG stream: %s", camera_service_->last_error().c_str());
        Stop();
        return false;
    }
    return true;
}

void WebRtcCameraService::Stop() {
    TaskHandle_t task = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!running_ && peer_ == nullptr) {
            return;
        }
        if (!stop_requested_) {
            terminal_state_ = ESP_PEER_STATE_CLOSED;
            stop_requested_ = true;
        }
        task = peer_task_;
    }

    if (task == xTaskGetCurrentTaskHandle()) {
        return;
    }
    if (task != nullptr) {
        // The peer task owns cleanup after leaving esp_peer_main_loop. Waiting
        // here also keeps callbacks from running against a destroyed service.
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(20));
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (peer_task_ == nullptr) break;
        }
        return;
    }
    FinishStop();
}

void WebRtcCameraService::RequestStop(esp_peer_state_t state) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!running_) {
        return;
    }
    if (!stop_requested_) {
        terminal_state_ = state;
        stop_requested_ = true;
    }
    channel_open_ = false;
}

void WebRtcCameraService::FinishStop() {
    camera_service_->StopJpegStream();
    esp_peer_handle_t peer = nullptr;
    StateCallback callback;
    esp_peer_state_t terminal_state = ESP_PEER_STATE_CLOSED;
    {
        std::lock_guard<std::recursive_mutex> api_lock(peer_api_mutex_);
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            peer = peer_;
            peer_ = nullptr;
            channel_open_ = false;
            channel_requested_ = false;
            signaling_callback_ = {};
            callback = std::move(state_callback_);
            state_callback_ = {};
            terminal_state = terminal_state_;
        }
        if (peer != nullptr) {
            esp_peer_close(peer);
        }
    }
    camera_service_->StopPreview(CameraService::PreviewOwner::kRemote);
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        running_ = false;
        // The peer task has not deleted itself yet; this is native cleanup only.
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

bool WebRtcCameraService::HandleRemoteMessage(esp_peer_msg_type_t type, const uint8_t* data,
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

bool WebRtcCameraService::IsRunning() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return running_;
}

bool WebRtcCameraService::IsChannelOpen() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return channel_open_;
}

int WebRtcCameraService::OnPeerState(esp_peer_state_t state, void* ctx) {
    auto* service = static_cast<WebRtcCameraService*>(ctx);
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
                channel_cfg.type = ESP_PEER_DATA_CHANNEL_RELIABLE;
                channel_cfg.ordered = true;
                channel_cfg.label = const_cast<char*>(kChannelLabel);
                if (esp_peer_create_data_channel(peer, &channel_cfg) != ESP_PEER_ERR_NONE) {
                    service->RequestStop(ESP_PEER_STATE_CONNECT_FAILED);
                    return ESP_PEER_ERR_NONE;
                }
            }
        }
        service->EmitState(state);
    }
    return ESP_PEER_ERR_NONE;
}

int WebRtcCameraService::OnPeerMessage(esp_peer_msg_t* message, void* ctx) {
    auto* service = static_cast<WebRtcCameraService*>(ctx);
    if (service != nullptr && message != nullptr) {
        service->EmitMessage(message);
    }
    return ESP_PEER_ERR_NONE;
}

int WebRtcCameraService::OnChannelOpen(esp_peer_data_channel_info_t* channel, void* ctx) {
    auto* service = static_cast<WebRtcCameraService*>(ctx);
    if (service == nullptr || channel == nullptr) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(service->mutex_);
        service->channel_open_ = !service->stop_requested_ && channel->label != nullptr &&
                                 std::strcmp(channel->label, kChannelLabel) == 0;
    }
    return ESP_PEER_ERR_NONE;
}

int WebRtcCameraService::OnChannelClose(esp_peer_data_channel_info_t* channel, void* ctx) {
    auto* service = static_cast<WebRtcCameraService*>(ctx);
    if (service != nullptr) {
        service->RequestStop(ESP_PEER_STATE_DATA_CHANNEL_CLOSED);
    }
    return ESP_PEER_ERR_NONE;
}

void WebRtcCameraService::PeerTaskEntry(void* arg) {
    auto* service = static_cast<WebRtcCameraService*>(arg);
    if (service != nullptr) {
        service->PeerTask();
    }
    vTaskDeleteWithCaps(nullptr);
}

void WebRtcCameraService::PeerTask() {
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
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    while (true) {
        esp_peer_handle_t peer = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (stop_requested_ || peer_ == nullptr) {
                break;
            }
            peer = peer_;
        }
        int ret = ESP_PEER_ERR_NONE;
        {
            // The default implementation is driven by one thread. Serialize
            // main_loop with signalling, data sends and close so no API call
            // uses a handle while FinishStop is releasing it.
            std::lock_guard<std::recursive_mutex> api_lock(peer_api_mutex_);
            ret = esp_peer_main_loop(peer);
        }
        if (ret != ESP_PEER_ERR_NONE && ret != ESP_PEER_ERR_WOULD_BLOCK) {
            ESP_LOGW(TAG, "esp_peer_main_loop failed: %d", ret);
            RequestStop(ESP_PEER_STATE_CONNECT_FAILED);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(kPeerLoopDelayMs));
    }
    FinishStop();
}

void WebRtcCameraService::HandleJpeg(std::vector<uint8_t>&& jpeg, uint32_t sequence,
                                     int64_t timestamp_us) {
    (void)sequence;
    (void)timestamp_us;
    if (jpeg.empty()) {
        return;
    }
    bool channel_open = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        channel_open = running_ && channel_open_ && !stop_requested_;
    }
    if (channel_open) {
        SendJpegChunks(jpeg);
    }
}

bool WebRtcCameraService::SendJpegChunks(const std::vector<uint8_t>& jpeg) {
    esp_peer_handle_t peer = nullptr;
    size_t chunk_size = kDefaultChunkSize;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (stop_requested_ || !channel_open_) {
            return false;
        }
        peer = peer_;
        chunk_size = config_.chunk_size == 0
                         ? kDefaultChunkSize
                         : std::min<size_t>(config_.chunk_size, kMaxChunkSize);
    }
    if (peer == nullptr || jpeg.size() == 0) {
        return false;
    }

    const size_t chunk_count = (jpeg.size() + chunk_size - 1) / chunk_size;
    if (chunk_count == 0 || chunk_count > (kChunkSequenceMask + 1u)) {
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
        const int64_t retry_deadline = esp_timer_get_time() + kDataSendRetryTimeoutUs;
        while (true) {
            int ret = ESP_PEER_ERR_WRONG_STATE;
            {
                // Do not hold the API lock while waiting for SCTP capacity;
                // PeerTask must be able to pump esp_peer_main_loop().
                std::lock_guard<std::recursive_mutex> api_lock(peer_api_mutex_);
                {
                    std::lock_guard<std::recursive_mutex> lock(mutex_);
                    if (stop_requested_ || !channel_open_ || peer_ == nullptr) {
                        return false;
                    }
                    peer = peer_;
                }
                ret = esp_peer_send_data(peer, &frame);
            }
            if (ret == ESP_PEER_ERR_NONE) {
                break;
            }
            if (ret != ESP_PEER_ERR_WOULD_BLOCK || esp_timer_get_time() >= retry_deadline) {
                ESP_LOGW(TAG, "Camera JPEG data send failed: %d", ret);
                return false;
            }
            vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(kDataSendRetryDelayMs)));
            int loop_ret = ESP_PEER_ERR_NONE;
            {
                std::lock_guard<std::recursive_mutex> api_lock(peer_api_mutex_);
                loop_ret = esp_peer_main_loop(peer);
            }
            if (loop_ret != ESP_PEER_ERR_NONE && loop_ret != ESP_PEER_ERR_WOULD_BLOCK) {
                ESP_LOGW(TAG, "Camera peer loop failed while sending JPEG: %d", loop_ret);
                return false;
            }
        }
        offset += payload_size;
    }
    return true;
}

void WebRtcCameraService::EmitState(esp_peer_state_t state) {
    StateCallback callback;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        callback = state_callback_;
    }
    if (callback) {
        callback(state);
    }
}

void WebRtcCameraService::EmitMessage(esp_peer_msg_t* message) {
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
