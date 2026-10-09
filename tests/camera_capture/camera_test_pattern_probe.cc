#include "host_runtime.h"
#include "../task_retirement/task_retirement_host.h"
#include "phone_os/camera_service.h"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    camera_host::Reset("");
    rodakos::CameraService camera(nullptr);
    if (!camera.StartPreview(2, 2)) return 2;
    for (unsigned attempt = 0; attempt < 1000 && !camera.GetState().has_frame; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!camera.GetState().has_frame) return 3;
    if (camera_host::test_pattern_calls != 1 || camera_host::test_pattern_value != 1) return 4;
    camera.StopPreview();
    camera_host::JoinTasks();
    if (camera_host::frame_mappings != 0 || retirement_host::Snapshot().live_tasks != 0) return 5;
    std::cout << "CAMERA_TEST_PATTERN_ENABLED" << std::endl;
    return 0;
}
