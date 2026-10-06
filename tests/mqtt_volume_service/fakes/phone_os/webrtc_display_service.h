#pragma once
#include "phone_os/webrtc_camera_service.h"
#include <string>
namespace rodakos {
class WebRtcDisplayService : public WebRtcCameraService {
public:
    using ControlReply = std::function<void(bool, const char*)>;
    using ControlCallback = std::function<void(const std::string&, ControlReply)>;
    bool Start(const Config& config, SignalingCallback signal, StateCallback state, ControlCallback) {
        return WebRtcCameraService::Start(config, std::move(signal), std::move(state));
    }
};
}
