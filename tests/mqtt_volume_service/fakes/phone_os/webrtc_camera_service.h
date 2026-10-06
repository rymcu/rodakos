#pragma once
#include <cstdint>
#include <functional>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
enum esp_peer_msg_type_t { ESP_PEER_MSG_TYPE_NONE, ESP_PEER_MSG_TYPE_SDP, ESP_PEER_MSG_TYPE_CANDIDATE };
enum esp_peer_state_t {
    ESP_PEER_STATE_NEW_CONNECTION, ESP_PEER_STATE_CONNECTING, ESP_PEER_STATE_CONNECTED,
    ESP_PEER_STATE_DATA_CHANNEL_CONNECTED, ESP_PEER_STATE_DISCONNECTED, ESP_PEER_STATE_CONNECT_FAILED,
    ESP_PEER_STATE_CLOSED, ESP_PEER_STATE_DATA_CHANNEL_CLOSED, ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED
};
namespace fake_stream {
inline std::atomic<unsigned> live_peers{0};
inline std::atomic<unsigned> maximum_live_peers{0};
inline void ObserveMaximum(std::atomic<unsigned>& maximum, unsigned value) {
    unsigned previous = maximum.load();
    while (value > previous && !maximum.compare_exchange_weak(previous, value)) {}
}
inline void Reset() { live_peers = 0; maximum_live_peers = 0; }
}
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
    bool synchronous_terminal_state = false;
    bool terminal_on_stop_thread = false;
    std::function<void()> before_start_return;
    std::function<void()> before_stop_return;
    std::function<void()> before_remote_return;
    std::atomic<unsigned> start_calls{0};
    std::atomic<unsigned> stop_calls{0};
    std::atomic<unsigned> remote_calls{0};
    std::atomic<unsigned> terminal_callback_returns{0};
    std::atomic<unsigned> operations_in_flight{0};
    std::atomic<unsigned> maximum_overlapping_operations{0};
    std::atomic<unsigned> live_instance{0};
    std::atomic<unsigned> last_remote_instance{0};
    std::atomic<bool> running{false};
    ~WebRtcCameraService() {
        if (running.exchange(false)) --fake_stream::live_peers;
    }
    bool Start(const Config&, SignalingCallback signal, StateCallback state) {
        Operation operation(*this);
        Callbacks callbacks{std::move(signal), std::move(state)};
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callbacks_.push_back(callbacks);
        }
        const unsigned instance = ++start_calls;
        if (synchronous_signal) callbacks.signal(ESP_PEER_MSG_TYPE_SDP, {'v', '=', '0', '\r', '\n'});
        if (synchronous_state) callbacks.state(ESP_PEER_STATE_CONNECTED);
        if (synchronous_terminal_state) callbacks.state(ESP_PEER_STATE_CLOSED);
        if (before_start_return) before_start_return();
        if (start_result) {
            live_instance = instance;
            if (!running.exchange(true))
                fake_stream::ObserveMaximum(fake_stream::maximum_live_peers, ++fake_stream::live_peers);
        }
        return start_result;
    }
    void Stop() {
        Operation operation(*this);
        ++stop_calls;
        StateCallback terminal;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (terminal_on_stop_thread && !callbacks_.empty()) terminal = callbacks_.back().state;
        }
        if (terminal) {
            std::thread callback([&]() {
                terminal(ESP_PEER_STATE_CLOSED);
                ++terminal_callback_returns;
            });
            callback.join();
        }
        if (before_stop_return) before_stop_return();
        if (running.exchange(false)) --fake_stream::live_peers;
        live_instance = 0;
    }
    bool HandleRemoteMessage(esp_peer_msg_type_t, const std::vector<uint8_t>&) {
        Operation operation(*this);
        ++remote_calls;
        last_remote_instance = live_instance.load();
        if (before_remote_return) before_remote_return();
        return remote_result && running;
    }
    Callbacks SavedCallbacks(size_t index = 0) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return callbacks_.at(index);
    }
private:
    struct Operation {
        WebRtcCameraService& peer;
        explicit Operation(WebRtcCameraService& value) : peer(value) {
            fake_stream::ObserveMaximum(peer.maximum_overlapping_operations, ++peer.operations_in_flight);
        }
        ~Operation() { --peer.operations_in_flight; }
    };
    std::mutex callback_mutex_;
    std::vector<Callbacks> callbacks_;
};
}
