#pragma once
#include <cstdint>
#include <string>
namespace rodakos {
struct MotionServiceState {
    bool backend_configured = false;
    bool active = false;
    uint32_t active_clients = 0;
    std::string sensor_name;
    std::string last_error;
};
class MotionService {
public:
    MotionServiceState GetState() const { return state; }
    MotionServiceState state;
};
}  // namespace rodakos
