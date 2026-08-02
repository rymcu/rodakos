#include "phone_os/voice_cloud_websocket_transport.h"

#include <cJSON.h>
#include <arpa/inet.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>

#include <limits>
#include <cstring>
#include <utility>
#include <vector>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceWs";
constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kHelloBit = BIT1;
constexpr EventBits_t kErrorBit = BIT2;
constexpr EventBits_t kCancelBit = BIT3;
constexpr int kConnectTimeoutMs = 10000;
constexpr int kHelloTimeoutMs = 10000;
constexpr int kSendTimeoutMs = 3000;
constexpr int kUplinkSampleRate = 16000;
constexpr int kUplinkChannels = 1;
constexpr int kUplinkFrameDurationMs = 60;
constexpr uint16_t kBinaryMessageTypeAudio = 0;
constexpr size_t kBinaryProtocol2HeaderSize = 16;
constexpr size_t kBinaryProtocol3HeaderSize = 4;
constexpr size_t kMaxInboundAudioMessageSize = 8 * 1024;
constexpr size_t kMaxInboundTextMessageSize = 64 * 1024;
constexpr uint32_t kCleanupTaskStackSize = 4096;
constexpr UBaseType_t kCleanupTaskPriority = 3;

class RecursiveSemaphoreLock {
public:
    explicit RecursiveSemaphoreLock(SemaphoreHandle_t semaphore) : semaphore_(semaphore) {
        locked_ = semaphore_ != nullptr &&
                  xSemaphoreTakeRecursive(semaphore_, portMAX_DELAY) == pdTRUE;
    }

    ~RecursiveSemaphoreLock() {
        if (locked_) {
            xSemaphoreGiveRecursive(semaphore_);
        }
    }

    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t semaphore_ = nullptr;
    bool locked_ = false;
};

class SemaphoreLock {
public:
    explicit SemaphoreLock(SemaphoreHandle_t semaphore) : semaphore_(semaphore) {
        locked_ = semaphore_ != nullptr &&
                  xSemaphoreTake(semaphore_, portMAX_DELAY) == pdTRUE;
    }

    ~SemaphoreLock() {
        if (locked_) {
            xSemaphoreGive(semaphore_);
        }
    }

    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t semaphore_ = nullptr;
    bool locked_ = false;
};

std::string JsonToString(cJSON* root) {
    char* json = cJSON_PrintUnformatted(root);
    if (json == nullptr) {
        return "{}";
    }
    std::string result(json);
    cJSON_free(json);
    return result;
}

std::string MacAddress() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buffer[18];
    std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buffer;
}

const char* ListeningModeName(VoiceListeningMode mode) {
    switch (mode) {
        case VoiceListeningMode::kRealtime:
            return "realtime";
        case VoiceListeningMode::kManualStop:
            return "manual";
        case VoiceListeningMode::kAutoStop:
        default:
            return "auto";
    }
}

std::vector<uint8_t> WrapAudioPacketV2(const VoiceAudioPacket& packet, int version) {
    std::vector<uint8_t> frame(kBinaryProtocol2HeaderSize + packet.payload.size());
    uint16_t wire_version = htons(static_cast<uint16_t>(version));
    uint16_t type = htons(kBinaryMessageTypeAudio);
    uint32_t reserved = 0;
    uint32_t timestamp = htonl(packet.timestamp_ms);
    uint32_t payload_size = htonl(static_cast<uint32_t>(packet.payload.size()));
    std::memcpy(frame.data(), &wire_version, sizeof(wire_version));
    std::memcpy(frame.data() + 2, &type, sizeof(type));
    std::memcpy(frame.data() + 4, &reserved, sizeof(reserved));
    std::memcpy(frame.data() + 8, &timestamp, sizeof(timestamp));
    std::memcpy(frame.data() + 12, &payload_size, sizeof(payload_size));
    std::memcpy(frame.data() + kBinaryProtocol2HeaderSize,
                packet.payload.data(), packet.payload.size());
    return frame;
}

std::vector<uint8_t> WrapAudioPacketV3(const VoiceAudioPacket& packet) {
    std::vector<uint8_t> frame(kBinaryProtocol3HeaderSize + packet.payload.size());
    frame[0] = static_cast<uint8_t>(kBinaryMessageTypeAudio);
    frame[1] = 0;
    uint16_t payload_size = htons(static_cast<uint16_t>(packet.payload.size()));
    std::memcpy(frame.data() + 2, &payload_size, sizeof(payload_size));
    std::memcpy(frame.data() + kBinaryProtocol3HeaderSize,
                packet.payload.data(), packet.payload.size());
    return frame;
}

bool UnwrapAudioPacket(const uint8_t* data,
                       size_t size,
                       int version,
                       VoiceAudioPacket& packet) {
    if (data == nullptr || size == 0) {
        return false;
    }

    size_t payload_offset = 0;
    size_t payload_size = size;
    if (version == 2) {
        if (size < kBinaryProtocol2HeaderSize) {
            return false;
        }
        uint32_t timestamp = 0;
        uint32_t wire_size = 0;
        std::memcpy(&timestamp, data + 8, sizeof(timestamp));
        std::memcpy(&wire_size, data + 12, sizeof(wire_size));
        packet.timestamp_ms = ntohl(timestamp);
        payload_offset = kBinaryProtocol2HeaderSize;
        payload_size = ntohl(wire_size);
    } else if (version == 3) {
        if (size < kBinaryProtocol3HeaderSize) {
            return false;
        }
        uint16_t wire_size = 0;
        std::memcpy(&wire_size, data + 2, sizeof(wire_size));
        payload_offset = kBinaryProtocol3HeaderSize;
        payload_size = ntohs(wire_size);
    } else {
        packet.timestamp_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    }

    if (payload_size == 0 || payload_offset + payload_size > size) {
        return false;
    }
    packet.payload.assign(data + payload_offset, data + payload_offset + payload_size);
    return true;
}

bool IsSupportedOpusSampleRate(int sample_rate) {
    return sample_rate == 8000 || sample_rate == 12000 || sample_rate == 16000 ||
           sample_rate == 24000 || sample_rate == 48000;
}

bool IsSupportedOpusFrameDuration(int frame_duration_ms) {
    return frame_duration_ms == 5 || frame_duration_ms == 10 ||
           frame_duration_ms == 20 || frame_duration_ms == 40 ||
           frame_duration_ms == 60;
}

}  // namespace

VoiceCloudWebSocketTransport::VoiceCloudWebSocketTransport(DeviceCloudConfigService& config_service)
    : config_service_(config_service) {
    mutex_ = xSemaphoreCreateMutex();
    client_mutex_ = xSemaphoreCreateRecursiveMutex();
    open_mutex_ = xSemaphoreCreateMutex();
    frame_mutex_ = xSemaphoreCreateMutex();
    events_ = xEventGroupCreate();
}

VoiceCloudWebSocketTransport::~VoiceCloudWebSocketTransport() {
    CloseAudioChannel();
    if (open_mutex_ != nullptr) {
        xSemaphoreTake(open_mutex_, portMAX_DELAY);
        xSemaphoreGive(open_mutex_);
        vSemaphoreDelete(open_mutex_);
        open_mutex_ = nullptr;
    }
    WaitForAudioChannelClosed();
    if (events_ != nullptr) {
        vEventGroupDelete(events_);
        events_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    if (client_mutex_ != nullptr) {
        vSemaphoreDelete(client_mutex_);
        client_mutex_ = nullptr;
    }
    if (frame_mutex_ != nullptr) {
        vSemaphoreDelete(frame_mutex_);
        frame_mutex_ = nullptr;
    }
}

bool VoiceCloudWebSocketTransport::Start() {
    if (started_) {
        return true;
    }
    if (mutex_ == nullptr || client_mutex_ == nullptr || open_mutex_ == nullptr ||
        frame_mutex_ == nullptr || events_ == nullptr) {
        SetError("Voice transport synchronization unavailable");
        return false;
    }
    started_ = true;
    return true;
}

bool VoiceCloudWebSocketTransport::OpenAudioChannel(VoiceOpenGuard can_continue) {
    if (mutex_ == nullptr || client_mutex_ == nullptr || open_mutex_ == nullptr ||
        frame_mutex_ == nullptr || events_ == nullptr) {
        SetError("Voice transport synchronization unavailable");
        return false;
    }
    SemaphoreLock open_lock(open_mutex_);
    if (!open_lock.locked()) {
        SetError("Transport lock unavailable");
        return false;
    }
    if (can_continue && !can_continue()) {
        SetError("Voice websocket open cancelled");
        return false;
    }

    CloseAudioChannel();
    WaitForAudioChannelClosed();
    if (can_continue && !can_continue()) {
        SetError("Voice websocket open cancelled");
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool cleanup_pending = closing_ || cleanup_in_progress_ ||
                                 cleanup_inline_required_ || cleanup_task_finished_ ||
                                 cleanup_task_ != nullptr || cleanup_client_ != nullptr;
    xSemaphoreGive(mutex_);
    if (cleanup_pending) {
        SetError("Previous voice websocket is still closing");
        return false;
    }

    uint32_t generation = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    generation = ++connection_generation_;
    closing_ = false;
    connected_ = false;
    channel_open_ = false;
    session_id_.clear();
    xSemaphoreGive(mutex_);
    xEventGroupClearBits(events_, kConnectedBit | kHelloBit | kErrorBit | kCancelBit);

    if (!IsConnectionCurrent(generation) || (can_continue && !can_continue())) {
        return false;
    }

    if (!config_service_.Load(config_)) {
        const std::string error = config_service_.last_error();
        SetError(error.empty() ? "Voice cloud config unavailable" : error);
        return false;
    }
    if (!IsConnectionCurrent(generation) || (can_continue && !can_continue())) {
        SetError("Voice websocket open cancelled");
        return false;
    }

    authorization_header_.clear();
    if (!config_.websocket_token.empty()) {
        authorization_header_ = config_.websocket_token.find(' ') == std::string::npos
            ? "Bearer " + config_.websocket_token
            : config_.websocket_token;
    }
    protocol_version_header_ = std::to_string(config_.websocket_version);
    device_id_header_ = MacAddress();
    client_id_header_ = config_service_.GetClientId();

    headers_.clear();
    if (!authorization_header_.empty()) {
        headers_ += "Authorization: " + authorization_header_ + "\r\n";
    }
    headers_ += "Protocol-Version: " + protocol_version_header_ + "\r\n";
    headers_ += "Device-Id: " + device_id_header_ + "\r\n";
    headers_ += "Client-Id: " + client_id_header_ + "\r\n";

    esp_websocket_client_config_t ws_config = {};
    ws_config.uri = config_.websocket_url.c_str();
    ws_config.headers = headers_.c_str();
    ws_config.disable_auto_reconnect = true;
    ws_config.buffer_size = 4096;
    ws_config.network_timeout_ms = 10000;
    ws_config.reconnect_timeout_ms = 10000;
    ws_config.pingpong_timeout_sec = 30;
    ws_config.task_name = "voice_ws";
    ws_config.task_stack = 6144;
    ws_config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_websocket_client_handle_t client = esp_websocket_client_init(&ws_config);
    if (client == nullptr) {
        SetError("Failed to create websocket client");
        return false;
    }
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, EventHandler, this);

    {
        RecursiveSemaphoreLock client_lock(client_mutex_);
        if (!client_lock.locked() || !IsConnectionCurrent(generation) ||
            (can_continue && !can_continue())) {
            esp_websocket_client_destroy(client);
            return false;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client_ = client;
        xSemaphoreGive(mutex_);
    }

    ESP_LOGI(TAG, "Connecting to voice cloud websocket: %s", config_.websocket_url.c_str());
    esp_err_t err = ESP_FAIL;
    {
        RecursiveSemaphoreLock client_lock(client_mutex_);
        if (!client_lock.locked() || !IsConnectionCurrent(generation) ||
            (can_continue && !can_continue())) {
            CloseAudioChannel();
            return false;
        }
        err = esp_websocket_client_start(client);
    }
    if (err != ESP_OK) {
        SetError(std::string("Websocket start failed: ") + esp_err_to_name(err));
        CloseAudioChannel();
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        events_, kConnectedBit | kErrorBit | kCancelBit, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(kConnectTimeoutMs));
    if ((bits & kCancelBit) != 0 || !IsConnectionCurrent(generation) ||
        (can_continue && !can_continue())) {
        SetError("Voice websocket open cancelled");
        CloseAudioChannel();
        return false;
    }
    if ((bits & kErrorBit) != 0 || (bits & kConnectedBit) == 0) {
        SetError((bits & kErrorBit) ? last_error() : "Websocket connect timeout");
        CloseAudioChannel();
        return false;
    }

    if (!IsConnectionCurrent(generation) || (can_continue && !can_continue())) {
        CloseAudioChannel();
        return false;
    }
    if (!SendHello(generation)) {
        CloseAudioChannel();
        return false;
    }

    if (!IsConnectionCurrent(generation) || (can_continue && !can_continue())) {
        CloseAudioChannel();
        return false;
    }
    bits = xEventGroupWaitBits(events_, kHelloBit | kErrorBit | kCancelBit, pdTRUE, pdFALSE,
                               pdMS_TO_TICKS(kHelloTimeoutMs));
    if ((bits & kCancelBit) != 0 || !IsConnectionCurrent(generation) ||
        (can_continue && !can_continue())) {
        SetError("Voice websocket hello cancelled");
        CloseAudioChannel();
        return false;
    }
    if ((bits & kErrorBit) != 0 || (bits & kHelloBit) == 0) {
        SetError((bits & kErrorBit) ? last_error() : "Voice cloud hello timeout");
        CloseAudioChannel();
        return false;
    }

    const bool allowed = !can_continue || can_continue();
    bool current = false;
    {
        RecursiveSemaphoreLock client_lock(client_mutex_);
        if (client_lock.locked() && mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            current = allowed && connection_generation_ == generation && !closing_ &&
                      client_ == client && connected_ &&
                      esp_websocket_client_is_connected(client);
            if (current) {
                channel_open_ = true;
                last_error_.clear();
            }
            xSemaphoreGive(mutex_);
        }
    }
    if (!current) {
        CloseAudioChannel();
        return false;
    }
    return true;
}

void VoiceCloudWebSocketTransport::CloseAudioChannel() {
    if (mutex_ == nullptr || client_mutex_ == nullptr) {
        const auto restore_closed_state = [this]() {
            ++connection_generation_;
            connected_ = false;
            channel_open_ = false;
            closing_ = false;
            cleanup_in_progress_ = false;
            cleanup_inline_required_ = false;
            cleanup_task_finished_ = false;
            cleanup_task_ = nullptr;
            client_ = nullptr;
            cleanup_client_ = nullptr;
            session_id_.clear();
        };
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            restore_closed_state();
            xSemaphoreGive(mutex_);
        } else {
            restore_closed_state();
        }
        if (events_ != nullptr) {
            xEventGroupSetBits(events_, kCancelBit | kErrorBit);
        }
        return;
    }

    esp_websocket_client_handle_t client_to_cleanup = nullptr;
    bool cleanup_started = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    closing_ = true;
    ++connection_generation_;
    xSemaphoreGive(mutex_);
    if (events_ != nullptr) {
        xEventGroupSetBits(events_, kCancelBit | kErrorBit);
    }

    {
        // OpenAudioChannel no longer holds this lock while waiting for network events, so
        // cancellation can detach the client even while connect/hello is pending.
        RecursiveSemaphoreLock client_lock(client_mutex_);
        if (!client_lock.locked()) {
            return;
        }

        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (!cleanup_in_progress_) {
                cleanup_client_ = client_;
                client_ = nullptr;
            }
            connected_ = false;
            channel_open_ = false;
            session_id_.clear();
            if (cleanup_client_ != nullptr && !cleanup_in_progress_) {
                cleanup_in_progress_ = true;
                cleanup_inline_required_ = false;
                cleanup_task_finished_ = false;
                cleanup_started = true;
                client_to_cleanup = cleanup_client_;
            }
            xSemaphoreGive(mutex_);
        } else {
            cleanup_client_ = client_;
            client_ = nullptr;
            cleanup_started = cleanup_client_ != nullptr;
            client_to_cleanup = cleanup_client_;
        }
    }

    if (cleanup_started) {
        esp_websocket_unregister_events(
            client_to_cleanup, WEBSOCKET_EVENT_ANY, EventHandler);
    }

    {
        SemaphoreLock frame_lock(frame_mutex_);
        if (frame_lock.locked()) {
            inbound_frame_.clear();
            inbound_opcode_ = 0;
            inbound_frame_offset_ = 0;
            inbound_message_active_ = false;
        }
    }

    if (cleanup_started) {
        TaskHandle_t cleanup_task = nullptr;
        BaseType_t created = pdFAIL;
        {
            RecursiveSemaphoreLock client_lock(client_mutex_);
            if (client_lock.locked()) {
                created = xTaskCreate(
                    CleanupTaskEntry, "voice_ws_gc", kCleanupTaskStackSize, this,
                    kCleanupTaskPriority, &cleanup_task);
                if (created == pdPASS) {
                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    cleanup_task_ = cleanup_task;
                    xSemaphoreGive(mutex_);
                }
            }
        }
        if (created != pdPASS) {
            SetError("Voice websocket cleanup task unavailable");
            xSemaphoreTake(mutex_, portMAX_DELAY);
            cleanup_task_ = nullptr;
            cleanup_task_finished_ = false;
            cleanup_inline_required_ = true;
            xSemaphoreGive(mutex_);
        }
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!cleanup_in_progress_) {
        closing_ = false;
    }
    xSemaphoreGive(mutex_);
}

void VoiceCloudWebSocketTransport::CleanupTaskEntry(void* arg) {
    auto* owner = static_cast<VoiceCloudWebSocketTransport*>(arg);
    if (owner != nullptr) {
        owner->CleanupDetachedClient();
        if (owner->mutex_ != nullptr) {
            xSemaphoreTake(owner->mutex_, portMAX_DELAY);
            owner->cleanup_task_finished_ = true;
            xSemaphoreGive(owner->mutex_);
        } else {
            owner->cleanup_task_finished_ = true;
        }
    }
    while (true) {
        vTaskSuspend(nullptr);
    }
}

void VoiceCloudWebSocketTransport::CleanupDetachedClient() {
    esp_websocket_client_handle_t client = nullptr;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client = cleanup_client_;
        xSemaphoreGive(mutex_);
    } else {
        client = cleanup_client_;
    }

    if (client != nullptr) {
        const int64_t cleanup_started_us = esp_timer_get_time();
        ESP_LOGI(TAG, "Voice websocket cleanup started");
        const esp_err_t stop_result = esp_websocket_client_stop(client);
        const int64_t stop_completed_us = esp_timer_get_time();
        ESP_LOGI(TAG,
                 "Voice websocket stop completed: result=%s duration_ms=%lld",
                 esp_err_to_name(stop_result),
                 static_cast<long long>((stop_completed_us - cleanup_started_us) / 1000));
        const esp_err_t destroy_result = esp_websocket_client_destroy(client);
        const int64_t destroy_completed_us = esp_timer_get_time();
        ESP_LOGI(TAG,
                 "Voice websocket destroy completed: result=%s duration_ms=%lld total_ms=%lld",
                 esp_err_to_name(destroy_result),
                 static_cast<long long>((destroy_completed_us - stop_completed_us) / 1000),
                 static_cast<long long>((destroy_completed_us - cleanup_started_us) / 1000));
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (cleanup_client_ == client) {
            cleanup_client_ = nullptr;
        }
        cleanup_in_progress_ = false;
        cleanup_inline_required_ = false;
        closing_ = false;
        xSemaphoreGive(mutex_);
    } else {
        cleanup_client_ = nullptr;
        cleanup_in_progress_ = false;
        cleanup_inline_required_ = false;
        closing_ = false;
    }
}

void VoiceCloudWebSocketTransport::WaitForAudioChannelClosed() {
    if (mutex_ == nullptr || client_mutex_ == nullptr) {
        return;
    }

    const int64_t wait_started_us = esp_timer_get_time();
    int64_t next_warning_us = wait_started_us + 5 * 1000 * 1000;
    while (true) {
        bool cleanup_inline = false;
        bool complete = false;
        TaskHandle_t cleanup_task = nullptr;
        bool closing = false;
        bool cleanup_in_progress = false;
        bool cleanup_inline_required = false;
        bool cleanup_task_finished = false;
        bool cleanup_client_pending = false;
        {
            RecursiveSemaphoreLock client_lock(client_mutex_);
            if (!client_lock.locked()) {
                return;
            }

            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (cleanup_inline_required_) {
                cleanup_inline_required_ = false;
                cleanup_inline = true;
            } else if (cleanup_task_ != nullptr && cleanup_task_finished_) {
                cleanup_task = cleanup_task_;
            }
            complete = !closing_ && !cleanup_in_progress_ &&
                       !cleanup_inline_required_ && !cleanup_task_finished_ &&
                       cleanup_task_ == nullptr && cleanup_client_ == nullptr;
            closing = closing_;
            cleanup_in_progress = cleanup_in_progress_;
            cleanup_inline_required = cleanup_inline_required_;
            cleanup_task_finished = cleanup_task_finished_;
            cleanup_client_pending = cleanup_client_ != nullptr;
            xSemaphoreGive(mutex_);

            if (cleanup_task != nullptr) {
                if (eTaskGetState(cleanup_task) == eSuspended) {
                    vTaskDelete(cleanup_task);
                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    if (cleanup_task_ == cleanup_task) {
                        cleanup_task_ = nullptr;
                        cleanup_task_finished_ = false;
                    }
                    xSemaphoreGive(mutex_);
                    continue;
                }
            }
            if (cleanup_inline) {
                CleanupDetachedClient();
                continue;
            }
            if (complete) {
                return;
            }
        }
        const int64_t now_us = esp_timer_get_time();
        if (now_us >= next_warning_us) {
            ESP_LOGW(TAG,
                     "Waiting for voice websocket cleanup: elapsed_ms=%lld closing=%d "
                     "in_progress=%d inline=%d task_finished=%d client_pending=%d",
                     static_cast<long long>((now_us - wait_started_us) / 1000),
                     closing, cleanup_in_progress, cleanup_inline_required,
                     cleanup_task_finished, cleanup_client_pending);
            next_warning_us = now_us + 5 * 1000 * 1000;
        }
        vTaskDelay(1);
    }
}

bool VoiceCloudWebSocketTransport::IsAudioChannelOpen() const {
    RecursiveSemaphoreLock client_lock(client_mutex_);
    if (!client_lock.locked() || mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool open = !closing_ && channel_open_ && client_ != nullptr &&
                      esp_websocket_client_is_connected(client_);
    xSemaphoreGive(mutex_);
    return open;
}

uint32_t VoiceCloudWebSocketTransport::connection_generation() const {
    if (mutex_ == nullptr) {
        return 0;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t generation = connection_generation_;
    xSemaphoreGive(mutex_);
    return generation;
}

bool VoiceCloudWebSocketTransport::SendAudio(const VoiceAudioPacket& packet,
                                             uint32_t expected_generation) {
    RecursiveSemaphoreLock client_lock(client_mutex_);
    if (!client_lock.locked()) {
        SetError("Transport lock unavailable");
        return false;
    }
    esp_websocket_client_handle_t client = nullptr;
    bool open = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client = client_;
        open = !closing_ && channel_open_ && connected_ && client != nullptr &&
               connection_generation_ == expected_generation;
        xSemaphoreGive(mutex_);
    }
    if (!open || !esp_websocket_client_is_connected(client)) {
        SetError("Audio channel is not open");
        return false;
    }
    if (packet.payload.empty()) {
        return true;
    }

    const uint8_t* data = packet.payload.data();
    size_t size = packet.payload.size();
    std::vector<uint8_t> framed_packet;
    if (config_.websocket_version == 2) {
        if (packet.payload.size() > std::numeric_limits<uint32_t>::max()) {
            SetError("Audio packet is too large");
            return false;
        }
        framed_packet = WrapAudioPacketV2(packet, config_.websocket_version);
        data = framed_packet.data();
        size = framed_packet.size();
    } else if (config_.websocket_version == 3) {
        if (packet.payload.size() > std::numeric_limits<uint16_t>::max()) {
            SetError("Audio packet is too large for protocol v3");
            return false;
        }
        framed_packet = WrapAudioPacketV3(packet);
        data = framed_packet.data();
        size = framed_packet.size();
    }

    int sent = esp_websocket_client_send_bin(
        client, reinterpret_cast<const char*>(data), size,
        pdMS_TO_TICKS(kSendTimeoutMs));
    if (sent != static_cast<int>(size)) {
        SetError("Failed to send complete audio packet");
        return false;
    }
    return true;
}

bool VoiceCloudWebSocketTransport::SendStartListening(VoiceListeningMode mode,
                                                      uint32_t expected_generation) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetError("Audio channel is not open");
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id.c_str());
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "start");
    cJSON_AddStringToObject(root, "mode", ListeningModeName(mode));
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    const bool sent = SendText(message, expected_generation, session_id, true);
    if (sent) {
        ESP_LOGI(TAG, "Sent listen:start: session=%s mode=%s",
                 session_id.c_str(), ListeningModeName(mode));
    }
    return sent;
}

bool VoiceCloudWebSocketTransport::SendStopListening(uint32_t expected_generation) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetError("Audio channel is not open");
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id.c_str());
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "stop");
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    const bool sent = SendText(message, expected_generation, session_id, true);
    if (sent) {
        ESP_LOGI(TAG, "Sent listen:stop: session=%s", session_id.c_str());
    }
    return sent;
}

bool VoiceCloudWebSocketTransport::SendWakeWordDetected(const std::string& wake_word,
                                                        uint32_t expected_generation) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetError("Audio channel is not open");
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id.c_str());
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "detect");
    cJSON_AddStringToObject(root, "text", wake_word.c_str());
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    const bool sent = SendText(message, expected_generation, session_id, true);
    if (sent) {
        ESP_LOGI(TAG, "Sent listen:detect: session=%s text=%s",
                 session_id.c_str(), wake_word.c_str());
    }
    return sent;
}

bool VoiceCloudWebSocketTransport::SendAbortSpeaking(VoiceAbortReason reason,
                                                     uint32_t expected_generation) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetError("Audio channel is not open");
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id.c_str());
    cJSON_AddStringToObject(root, "type", "abort");
    if (reason == VoiceAbortReason::kWakeWordDetected) {
        cJSON_AddStringToObject(root, "reason", "wake_word_detected");
    }
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return SendText(message, expected_generation, session_id, true);
}

bool VoiceCloudWebSocketTransport::SendMcpMessage(const std::string& payload,
                                                  uint32_t expected_generation) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetError("Audio channel is not open");
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id.c_str());
    cJSON_AddStringToObject(root, "type", "mcp");
    cJSON* payload_json = cJSON_Parse(payload.c_str());
    if (payload_json != nullptr) {
        cJSON_AddItemToObject(root, "payload", payload_json);
    } else {
        cJSON_AddStringToObject(root, "payload", payload.c_str());
    }
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return SendText(message, expected_generation, session_id, true);
}

void VoiceCloudWebSocketTransport::SetInboundHandler(VoiceInboundHandler handler) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        inbound_handler_ = std::move(handler);
        xSemaphoreGive(mutex_);
    } else {
        inbound_handler_ = std::move(handler);
    }
}

void VoiceCloudWebSocketTransport::EventHandler(void* arg,
                                             esp_event_base_t,
                                             int32_t event_id,
                                             void* event_data) {
    auto* self = static_cast<VoiceCloudWebSocketTransport*>(arg);
    auto* data = static_cast<esp_websocket_event_data_t*>(event_data);
    if (self == nullptr) {
        return;
    }
    uint32_t generation = 0;
    if (self->mutex_ != nullptr) {
        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        const bool current = !self->closing_ && self->client_ != nullptr &&
                             (data == nullptr || data->client == nullptr ||
                              self->client_ == data->client);
        generation = self->connection_generation_;
        xSemaphoreGive(self->mutex_);
        if (!current) {
            return;
        }
    }

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            if (self->mutex_ != nullptr) {
                xSemaphoreTake(self->mutex_, portMAX_DELAY);
                self->connected_ = true;
                xSemaphoreGive(self->mutex_);
            }
            xEventGroupSetBits(self->events_, kConnectedBit);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED: {
            bool unexpected_close = false;
            if (self->mutex_ != nullptr) {
                xSemaphoreTake(self->mutex_, portMAX_DELAY);
                unexpected_close = !self->closing_ &&
                                   (self->connected_ || self->channel_open_);
                self->connected_ = false;
                self->channel_open_ = false;
                xSemaphoreGive(self->mutex_);
            }
            if (unexpected_close) {
                self->SetError("Voice websocket disconnected");
                xEventGroupSetBits(self->events_, kErrorBit);
                self->EmitInbound(VoiceInboundEvent{
                    .type = VoiceInboundEventType::kError,
                    .audio = {},
                    .payload = "Voice websocket disconnected",
                }, generation);
            }
            break;
        }
        case WEBSOCKET_EVENT_ERROR:
            self->SetError("Websocket error");
            xEventGroupSetBits(self->events_, kErrorBit);
            self->EmitInbound(VoiceInboundEvent{
                .type = VoiceInboundEventType::kError,
                .audio = {},
                .payload = "Websocket error",
            }, generation);
            break;
        case WEBSOCKET_EVENT_DATA:
            if (data != nullptr) {
                self->HandleDataFrame(*data, generation);
            }
            break;
        default:
            break;
    }
}

void VoiceCloudWebSocketTransport::HandleDataFrame(
    const esp_websocket_event_data_t& data,
    uint32_t generation) {
    if (data.data_ptr == nullptr || data.data_len <= 0 || data.payload_len <= 0 ||
        data.payload_offset < 0 || data.payload_offset + data.data_len > data.payload_len) {
        return;
    }

    if (data.op_code >= 0x08) {
        return;
    }

    std::vector<uint8_t> complete_frame;
    uint8_t complete_opcode = 0;
    {
        SemaphoreLock frame_lock(frame_mutex_);
        if (!frame_lock.locked()) {
            return;
        }

        if (data.payload_offset == 0) {
            inbound_frame_offset_ = 0;
            if (data.op_code == WS_TRANSPORT_OPCODES_TEXT ||
                data.op_code == WS_TRANSPORT_OPCODES_BINARY) {
                inbound_frame_.clear();
                inbound_opcode_ = data.op_code;
                inbound_message_active_ = true;
            } else if (data.op_code != WS_TRANSPORT_OPCODES_CONT || !inbound_message_active_) {
                inbound_frame_.clear();
                inbound_opcode_ = 0;
                inbound_message_active_ = false;
                return;
            }
        }

        const size_t message_limit = inbound_opcode_ == WS_TRANSPORT_OPCODES_BINARY
                                         ? kMaxInboundAudioMessageSize
                                         : kMaxInboundTextMessageSize;
        if (!inbound_message_active_ ||
            static_cast<size_t>(data.payload_offset) != inbound_frame_offset_ ||
            inbound_frame_.size() + static_cast<size_t>(data.data_len) > message_limit) {
            inbound_frame_.clear();
            inbound_opcode_ = 0;
            inbound_frame_offset_ = 0;
            inbound_message_active_ = false;
            return;
        }

        const auto* chunk_begin = reinterpret_cast<const uint8_t*>(data.data_ptr);
        inbound_frame_.insert(inbound_frame_.end(),
                              chunk_begin,
                              chunk_begin + static_cast<size_t>(data.data_len));
        inbound_frame_offset_ += static_cast<size_t>(data.data_len);
        if (data.payload_offset + data.data_len < data.payload_len) {
            return;
        }
        inbound_frame_offset_ = 0;
        if (!data.fin) {
            return;
        }

        complete_opcode = inbound_opcode_;
        complete_frame = std::move(inbound_frame_);
        inbound_opcode_ = 0;
        inbound_message_active_ = false;
    }

    if (complete_opcode == WS_TRANSPORT_OPCODES_TEXT) {
        HandleTextFrame(reinterpret_cast<const char*>(complete_frame.data()),
                        static_cast<int>(complete_frame.size()), generation);
    } else if (complete_opcode == WS_TRANSPORT_OPCODES_BINARY) {
        HandleBinaryFrame(complete_frame.data(), complete_frame.size(), generation);
    }
}

bool VoiceCloudWebSocketTransport::SendText(const std::string& text,
                                            uint32_t expected_generation,
                                            const std::string& expected_session,
                                            bool require_open) {
    RecursiveSemaphoreLock client_lock(client_mutex_);
    if (!client_lock.locked()) {
        SetError("Transport lock unavailable");
        return false;
    }
    esp_websocket_client_handle_t client = nullptr;
    bool connected = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client = client_;
        connected = !closing_ && connected_ && client != nullptr &&
                    connection_generation_ == expected_generation &&
                    (!require_open ||
                     (channel_open_ && session_id_ == expected_session));
        xSemaphoreGive(mutex_);
    }
    if (!connected || !esp_websocket_client_is_connected(client)) {
        SetError("Websocket is not connected");
        return false;
    }
    const int sent = esp_websocket_client_send_text(client, text.c_str(), text.size(),
                                                    pdMS_TO_TICKS(kSendTimeoutMs));
    if (sent != static_cast<int>(text.size())) {
        SetError("Failed to send complete websocket text");
        return false;
    }
    ESP_LOGD(TAG, "Sent text: %s", text.c_str());
    return true;
}

bool VoiceCloudWebSocketTransport::SendHello(uint32_t generation) {
    return SendText(BuildHelloMessage(), generation, {}, false);
}

bool VoiceCloudWebSocketTransport::SnapshotSession(uint32_t expected_generation,
                                                   std::string& session_id) const {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool valid = !closing_ && connected_ && channel_open_ && client_ != nullptr &&
                       connection_generation_ == expected_generation &&
                       !session_id_.empty();
    if (valid) {
        session_id = session_id_;
    }
    xSemaphoreGive(mutex_);
    return valid;
}

std::string VoiceCloudWebSocketTransport::BuildHelloMessage() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", config_.websocket_version);
    cJSON_AddStringToObject(root, "transport", "websocket");

    cJSON* features = cJSON_CreateObject();
    cJSON_AddBoolToObject(features, "mcp", false);
    cJSON_AddItemToObject(root, "features", features);

    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", kUplinkSampleRate);
    cJSON_AddNumberToObject(audio_params, "download_sample_rate", server_sample_rate_);
    cJSON_AddNumberToObject(audio_params, "channels", kUplinkChannels);
    cJSON_AddNumberToObject(audio_params, "frame_duration", kUplinkFrameDurationMs);
    cJSON_AddItemToObject(root, "audio_params", audio_params);

    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return message;
}

void VoiceCloudWebSocketTransport::HandleTextFrame(const char* data,
                                                   int len,
                                                   uint32_t generation) {
    if (data == nullptr || len <= 0) {
        return;
    }
    std::string payload(data, len);
    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Invalid JSON from voice cloud: %.*s", len, data);
        return;
    }

    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && std::strcmp(type->valuestring, "hello") == 0) {
        ParseServerHello(payload, generation);
    } else if (cJSON_IsString(type) && std::strcmp(type->valuestring, "tts") == 0) {
        cJSON* state = cJSON_GetObjectItem(root, "state");
        if (cJSON_IsString(state) && std::strcmp(state->valuestring, "start") == 0) {
            EmitInbound(VoiceInboundEvent{
                .type = VoiceInboundEventType::kSpeakingStarted,
                .audio = {},
                .payload = {},
            }, generation);
        } else if (cJSON_IsString(state) && std::strcmp(state->valuestring, "stop") == 0) {
            EmitInbound(VoiceInboundEvent{
                .type = VoiceInboundEventType::kSpeakingStopped,
                .audio = {},
                .payload = {},
            }, generation);
        }
    } else if (cJSON_IsString(type) && std::strcmp(type->valuestring, "goodbye") == 0) {
        EmitInbound(VoiceInboundEvent{
            .type = VoiceInboundEventType::kSessionFinished,
            .audio = {},
            .payload = {},
        }, generation);
    } else if (cJSON_IsString(type) && std::strcmp(type->valuestring, "mcp") == 0) {
        EmitInbound(VoiceInboundEvent{
            .type = VoiceInboundEventType::kMcp,
            .audio = {},
            .payload = payload,
        }, generation);
    } else if (cJSON_IsString(type) && std::strcmp(type->valuestring, "error") == 0) {
        EmitInbound(VoiceInboundEvent{
            .type = VoiceInboundEventType::kError,
            .audio = {},
            .payload = payload,
        }, generation);
    } else if (cJSON_IsString(type)) {
        ESP_LOGI(TAG, "Voice cloud message: type=%s", type->valuestring);
    }
    cJSON_Delete(root);
}

void VoiceCloudWebSocketTransport::HandleBinaryFrame(const uint8_t* data,
                                                     size_t size,
                                                     uint32_t generation) {
    VoiceAudioPacket packet;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        packet.sample_rate = server_sample_rate_;
        packet.frame_duration_ms = server_frame_duration_ms_;
        xSemaphoreGive(mutex_);
    } else {
        packet.sample_rate = server_sample_rate_;
        packet.frame_duration_ms = server_frame_duration_ms_;
    }
    if (!UnwrapAudioPacket(data, size, config_.websocket_version, packet)) {
        SetError("Invalid voice cloud audio packet");
        EmitInbound(VoiceInboundEvent{
            .type = VoiceInboundEventType::kError,
            .audio = {},
            .payload = "Invalid voice cloud audio packet",
        }, generation);
        return;
    }
    EmitInbound(VoiceInboundEvent{
        .type = VoiceInboundEventType::kAudio,
        .audio = std::move(packet),
        .payload = {},
    }, generation);
}

void VoiceCloudWebSocketTransport::ParseServerHello(const std::string& payload,
                                                     uint32_t generation) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        SetError("Invalid server hello");
        xEventGroupSetBits(events_, kErrorBit);
        return;
    }

    cJSON* transport = cJSON_GetObjectItem(root, "transport");
    if (!cJSON_IsString(transport) || std::strcmp(transport->valuestring, "websocket") != 0) {
        cJSON_Delete(root);
        SetError("Unsupported voice cloud transport");
        xEventGroupSetBits(events_, kErrorBit);
        return;
    }

    cJSON* session_id = cJSON_GetObjectItem(root, "session_id");
    cJSON* audio_params = cJSON_GetObjectItem(root, "audio_params");
    cJSON* format = cJSON_IsObject(audio_params)
                        ? cJSON_GetObjectItem(audio_params, "format")
                        : nullptr;
    cJSON* channels = cJSON_IsObject(audio_params)
                          ? cJSON_GetObjectItem(audio_params, "channels")
                          : nullptr;
    cJSON* sample_rate = cJSON_IsObject(audio_params)
                             ? cJSON_GetObjectItem(audio_params, "download_sample_rate")
                             : nullptr;
    if (!cJSON_IsNumber(sample_rate) && cJSON_IsObject(audio_params)) {
        sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
    }
    cJSON* frame_duration = cJSON_IsObject(audio_params)
                                ? cJSON_GetObjectItem(audio_params, "frame_duration")
                                : nullptr;

    const bool valid_session = cJSON_IsString(session_id) &&
                               session_id->valuestring != nullptr &&
                               session_id->valuestring[0] != '\0';
    const bool valid_format = cJSON_IsString(format) &&
                              std::strcmp(format->valuestring, "opus") == 0;
    const bool valid_channels = cJSON_IsNumber(channels) && channels->valueint == 1;
    const int negotiated_sample_rate = cJSON_IsNumber(sample_rate)
                                           ? sample_rate->valueint
                                           : 0;
    const int negotiated_frame_duration = cJSON_IsNumber(frame_duration)
                                              ? frame_duration->valueint
                                              : 0;
    if (!valid_session || !valid_format || !valid_channels ||
        !IsSupportedOpusSampleRate(negotiated_sample_rate) ||
        !IsSupportedOpusFrameDuration(negotiated_frame_duration)) {
        cJSON_Delete(root);
        SetError("Invalid voice cloud hello parameters");
        xEventGroupSetBits(events_, kErrorBit);
        return;
    }

    const std::string negotiated_session_id = session_id->valuestring;
    cJSON_Delete(root);

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (closing_ || client_ == nullptr || connection_generation_ != generation) {
            xSemaphoreGive(mutex_);
            return;
        }
        session_id_ = negotiated_session_id;
        server_sample_rate_ = negotiated_sample_rate;
        server_frame_duration_ms_ = negotiated_frame_duration;
        xSemaphoreGive(mutex_);
    } else {
        session_id_ = negotiated_session_id;
        server_sample_rate_ = negotiated_sample_rate;
        server_frame_duration_ms_ = negotiated_frame_duration;
    }
    ESP_LOGI(TAG, "Voice cloud hello received: session=%s sample_rate=%d frame=%d",
             negotiated_session_id.c_str(), negotiated_sample_rate,
             negotiated_frame_duration);
    xEventGroupSetBits(events_, kHelloBit);
}

void VoiceCloudWebSocketTransport::SetError(const std::string& message) {
    const std::string normalized = message.empty() ? "Voice cloud transport error" : message;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        last_error_ = normalized;
        xSemaphoreGive(mutex_);
    } else {
        last_error_ = normalized;
    }
    ESP_LOGW(TAG, "%s", normalized.c_str());
}

std::string VoiceCloudWebSocketTransport::last_error() const {
    if (mutex_ == nullptr) {
        return last_error_;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const std::string error = last_error_;
    xSemaphoreGive(mutex_);
    return error;
}

bool VoiceCloudWebSocketTransport::IsConnectionCurrent(uint32_t generation) const {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool current = connection_generation_ == generation && !closing_;
    xSemaphoreGive(mutex_);
    return current;
}

void VoiceCloudWebSocketTransport::EmitInbound(VoiceInboundEvent&& event,
                                               uint32_t generation) {
    event.transport_generation = generation;
    VoiceInboundHandler handler;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        handler = inbound_handler_;
        xSemaphoreGive(mutex_);
    } else {
        handler = inbound_handler_;
    }
    if (handler) {
        handler(std::move(event));
    }
}

}  // namespace rodakos
