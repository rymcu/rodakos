#pragma once
#include "phone_os/webrtc_camera_service.h"
#include <string>
namespace rodakos {
class WebRtcDisplayService : public WebRtcCameraService {
public:
    using ControlReply = std::function<void(bool, const char*)>;
    using ControlCallback = std::function<void(const std::string&, ControlReply)>;
    bool Start(const Config&, SignalingCallback, StateCallback, ControlCallback) { return false; }
};
}
