#pragma once
#include <cstdint>
#include <functional>
#include <vector>
enum esp_peer_msg_type_t { ESP_PEER_MSG_TYPE_NONE, ESP_PEER_MSG_TYPE_SDP, ESP_PEER_MSG_TYPE_CANDIDATE };
enum esp_peer_state_t {
    ESP_PEER_STATE_NEW_CONNECTION, ESP_PEER_STATE_CONNECTING, ESP_PEER_STATE_CONNECTED,
    ESP_PEER_STATE_DATA_CHANNEL_CONNECTED, ESP_PEER_STATE_DISCONNECTED, ESP_PEER_STATE_CONNECT_FAILED,
    ESP_PEER_STATE_CLOSED, ESP_PEER_STATE_DATA_CHANNEL_CLOSED, ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED
};
namespace rodakos {
class WebRtcCameraService {
public:
    struct Config { int width = 320; int height = 240; uint8_t fps = 5; uint16_t chunk_size = 10000; };
    using SignalingCallback = std::function<void(esp_peer_msg_type_t, std::vector<uint8_t>&&)>;
    using StateCallback = std::function<void(esp_peer_state_t)>;
    bool Start(const Config&, SignalingCallback, StateCallback) { return false; }
    void Stop() {}
    bool HandleRemoteMessage(esp_peer_msg_type_t, const std::vector<uint8_t>&) { return false; }
};
}
