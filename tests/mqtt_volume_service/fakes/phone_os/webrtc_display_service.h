#pragma once
#include "phone_os/webrtc_camera_service.h"
#include "phone_os/stream_lease.h"
#include <string>
namespace rodakos {
class WebRtcDisplayService : public WebRtcCameraService {
public:
    struct Config : WebRtcCameraService::Config {
        StreamLeasePtr stream_lease;
    };
    using ControlReply = std::function<void(bool, const char*)>;
    using ControlCallback = std::function<void(const std::string&, ControlReply)>;
    bool Start(const Config& config, SignalingCallback signal, StateCallback state, ControlCallback control) {
        {
            std::lock_guard<std::mutex> lock(control_mutex_);
            controls_.push_back(std::move(control));
            leases_.push_back(config.stream_lease);
        }
        return WebRtcCameraService::Start(config, std::move(signal), std::move(state));
    }
    ControlCallback SavedControl(size_t index = 0) {
        std::lock_guard<std::mutex> lock(control_mutex_);
        return controls_.at(index);
    }
    StreamLeasePtr SavedLease(size_t index = 0) {
        std::lock_guard<std::mutex> lock(control_mutex_);
        return leases_.at(index);
    }
private:
    std::mutex control_mutex_;
    std::vector<ControlCallback> controls_;
    std::vector<StreamLeasePtr> leases_;
};
}
