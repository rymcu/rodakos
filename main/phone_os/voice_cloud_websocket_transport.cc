#include "phone_os/voice_cloud_websocket_transport.h"

#include <cJSON.h>
#include <arpa/inet.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <esp_mac.h>

#include <limits>
#include <cstring>
#include <vector>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceWs";
constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kHelloBit = BIT1;
constexpr EventBits_t kErrorBit = BIT2;
constexpr int kConnectTimeoutMs = 10000;
constexpr int kHelloTimeoutMs = 10000;
constexpr int kSendTimeoutMs = 3000;
constexpr int kUplinkSampleRate = 16000;
constexpr int kUplinkChannels = 1;
constexpr int kUplinkFrameDurationMs = 60;
constexpr uint16_t kBinaryMessageTypeAudio = 0;
constexpr size_t kBinaryProtocol2HeaderSize = 16;
constexpr size_t kBinaryProtocol3HeaderSize = 4;

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

}  // namespace

VoiceCloudWebSocketTransport::VoiceCloudWebSocketTransport(VoiceCloudConfigService& config_service)
    : config_service_(config_service) {
    mutex_ = xSemaphoreCreateMutex();
    events_ = xEventGroupCreate();
}

VoiceCloudWebSocketTransport::~VoiceCloudWebSocketTransport() {
    CloseAudioChannel();
    if (events_ != nullptr) {
        vEventGroupDelete(events_);
        events_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool VoiceCloudWebSocketTransport::Start() {
    if (started_) {
        return true;
    }
    started_ = true;
    return true;
}

bool VoiceCloudWebSocketTransport::OpenAudioChannel() {
    CloseAudioChannel();
    if (events_ == nullptr) {
        SetError("Transport events unavailable");
        return false;
    }
    xEventGroupClearBits(events_, kConnectedBit | kHelloBit | kErrorBit);

    if (!config_service_.Load(config_) && !config_service_.RefreshFromOta(config_)) {
        SetError(config_service_.last_error());
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

    client_ = esp_websocket_client_init(&ws_config);
    if (client_ == nullptr) {
        SetError("Failed to create websocket client");
        return false;
    }
    esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY, EventHandler, this);

    ESP_LOGI(TAG, "Connecting to voice cloud websocket: %s", config_.websocket_url.c_str());
    esp_err_t err = esp_websocket_client_start(client_);
    if (err != ESP_OK) {
        SetError(std::string("Websocket start failed: ") + esp_err_to_name(err));
        CloseAudioChannel();
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(events_, kConnectedBit | kErrorBit, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(kConnectTimeoutMs));
    if ((bits & kConnectedBit) == 0) {
        SetError((bits & kErrorBit) ? last_error_ : "Websocket connect timeout");
        CloseAudioChannel();
        return false;
    }

    if (!SendHello()) {
        CloseAudioChannel();
        return false;
    }

    bits = xEventGroupWaitBits(events_, kHelloBit | kErrorBit, pdTRUE, pdFALSE,
                               pdMS_TO_TICKS(kHelloTimeoutMs));
    if ((bits & kHelloBit) == 0) {
        SetError((bits & kErrorBit) ? last_error_ : "Voice cloud hello timeout");
        CloseAudioChannel();
        return false;
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        channel_open_ = true;
        xSemaphoreGive(mutex_);
    }
    last_error_.clear();
    return true;
}

void VoiceCloudWebSocketTransport::CloseAudioChannel() {
    esp_websocket_client_handle_t client = client_;
    client_ = nullptr;

    if (client != nullptr) {
        esp_websocket_client_close(client, pdMS_TO_TICKS(1000));
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        connected_ = false;
        channel_open_ = false;
        session_id_.clear();
        xSemaphoreGive(mutex_);
    }
}

bool VoiceCloudWebSocketTransport::IsAudioChannelOpen() const {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool open = channel_open_ && client_ != nullptr &&
                      esp_websocket_client_is_connected(client_);
    xSemaphoreGive(mutex_);
    return open;
}

bool VoiceCloudWebSocketTransport::SendAudio(const VoiceAudioPacket& packet) {
    if (!IsAudioChannelOpen()) {
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
        client_, reinterpret_cast<const char*>(data), size,
        pdMS_TO_TICKS(kSendTimeoutMs));
    if (sent < 0) {
        SetError("Failed to send audio");
        return false;
    }
    return true;
}

bool VoiceCloudWebSocketTransport::SendStartListening(VoiceListeningMode mode) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "start");
    cJSON_AddStringToObject(root, "mode", ListeningModeName(mode));
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return SendText(message);
}

bool VoiceCloudWebSocketTransport::SendStopListening() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "stop");
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return SendText(message);
}

bool VoiceCloudWebSocketTransport::SendWakeWordDetected(const std::string& wake_word) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "detect");
    cJSON_AddStringToObject(root, "text", wake_word.c_str());
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return SendText(message);
}

bool VoiceCloudWebSocketTransport::SendAbortSpeaking(VoiceAbortReason reason) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(root, "type", "abort");
    if (reason == VoiceAbortReason::kWakeWordDetected) {
        cJSON_AddStringToObject(root, "reason", "wake_word_detected");
    }
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return SendText(message);
}

bool VoiceCloudWebSocketTransport::SendMcpMessage(const std::string& payload) {
    if (!IsAudioChannelOpen()) {
        SetError("Audio channel is not open");
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(root, "type", "mcp");
    cJSON* payload_json = cJSON_Parse(payload.c_str());
    if (payload_json != nullptr) {
        cJSON_AddItemToObject(root, "payload", payload_json);
    } else {
        cJSON_AddStringToObject(root, "payload", payload.c_str());
    }
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return SendText(message);
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
        case WEBSOCKET_EVENT_CLOSED:
            if (self->mutex_ != nullptr) {
                xSemaphoreTake(self->mutex_, portMAX_DELAY);
                self->connected_ = false;
                self->channel_open_ = false;
                xSemaphoreGive(self->mutex_);
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            self->SetError("Websocket error");
            xEventGroupSetBits(self->events_, kErrorBit);
            break;
        case WEBSOCKET_EVENT_DATA:
            if (data != nullptr && data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
                self->HandleTextFrame(data->data_ptr, data->data_len);
            }
            break;
        default:
            break;
    }
}

bool VoiceCloudWebSocketTransport::SendText(const std::string& text) {
    if (client_ == nullptr || !esp_websocket_client_is_connected(client_)) {
        SetError("Websocket is not connected");
        return false;
    }
    const int sent = esp_websocket_client_send_text(client_, text.c_str(), text.size(),
                                                    pdMS_TO_TICKS(kSendTimeoutMs));
    if (sent < 0) {
        SetError("Failed to send websocket text");
        return false;
    }
    ESP_LOGD(TAG, "Sent text: %s", text.c_str());
    return true;
}

bool VoiceCloudWebSocketTransport::SendHello() {
    return SendText(BuildHelloMessage());
}

std::string VoiceCloudWebSocketTransport::BuildHelloMessage() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", config_.websocket_version);
    cJSON_AddStringToObject(root, "transport", "websocket");

    cJSON* features = cJSON_CreateObject();
    cJSON_AddBoolToObject(features, "mcp", true);
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

void VoiceCloudWebSocketTransport::HandleTextFrame(const char* data, int len) {
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
        ParseServerHello(payload);
    } else if (cJSON_IsString(type)) {
        ESP_LOGI(TAG, "Voice cloud message: type=%s", type->valuestring);
    }
    cJSON_Delete(root);
}

void VoiceCloudWebSocketTransport::ParseServerHello(const std::string& payload) {
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
    if (cJSON_IsString(session_id) && session_id->valuestring != nullptr) {
        session_id_ = session_id->valuestring;
    }

    cJSON* audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        cJSON* sample_rate = cJSON_GetObjectItem(audio_params, "download_sample_rate");
        if (!cJSON_IsNumber(sample_rate)) {
            sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        }
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
        }
        cJSON* frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ms_ = frame_duration->valueint;
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Voice cloud hello received: session=%s sample_rate=%d frame=%d",
             session_id_.c_str(), server_sample_rate_, server_frame_duration_ms_);
    xEventGroupSetBits(events_, kHelloBit);
}

void VoiceCloudWebSocketTransport::SetError(const std::string& message) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        last_error_ = message.empty() ? "Voice cloud transport error" : message;
        xSemaphoreGive(mutex_);
    } else {
        last_error_ = message;
    }
    ESP_LOGW(TAG, "%s", last_error_.c_str());
}

}  // namespace rodakos
