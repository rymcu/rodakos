#pragma once
#include <cstdint>
#include <string>
namespace rodakos {
struct CameraState {
    bool available = false;
    bool preview_running = false;
    bool has_frame = false;
    int width = 0;
    int height = 0;
    uint32_t frame_count = 0;
    std::string last_saved_path;
    std::string last_error;
};
class CameraService {
public:
    CameraState GetState() const { return state; }
    CameraState state;
};
}  // namespace rodakos
