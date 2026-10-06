#pragma once
#include <cstdint>
#include <functional>
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
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
    struct Callbacks { SignalingCallback signal; StateCallback state; };
    bool start_result = false;
    bool remote_result = false;
    bool synchronous_signal = false;
    bool synchronous_state = false;
    std::function<void()> before_start_return;
    std::atomic<unsigned> start_calls{0};
    std::atomic<unsigned> stop_calls{0};
    bool Start(const Config&, SignalingCallback signal, StateCallback state) {
        Callbacks callbacks{std::move(signal), std::move(state)};
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callbacks_.push_back(callbacks);
        }
        ++start_calls;
        if (synchronous_signal) callbacks.signal(ESP_PEER_MSG_TYPE_SDP, {'v', '=', '0', '\r', '\n'});
        if (synchronous_state) callbacks.state(ESP_PEER_STATE_CONNECTED);
        if (before_start_return) before_start_return();
        return start_result;
    }
    void Stop() { ++stop_calls; }
    bool HandleRemoteMessage(esp_peer_msg_type_t, const std::vector<uint8_t>&) { return remote_result; }
    Callbacks SavedCallbacks(size_t index = 0) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return callbacks_.at(index);
    }
private:
    std::mutex callback_mutex_;
    std::vector<Callbacks> callbacks_;
};
}
