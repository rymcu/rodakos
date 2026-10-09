#include "host_runtime.h"
#include "../task_retirement/task_retirement_host.h"
#include "phone_os/camera_service.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

int main() {
    camera_host::Reset("");
    std::atomic<unsigned> snapshots{0};
    camera_host::log_hook = [&](const char* format) {
        if (std::strstr(format, "camera_sensor_registers=1") != nullptr) {
            ++snapshots;
        }
    };

    rodakos::CameraService camera(nullptr);
    if (!camera.StartPreview(2, 2)) return 2;
    for (unsigned attempt = 0; attempt < 1000 && !camera.GetState().has_frame; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!camera.GetState().has_frame) return 3;

    for (unsigned attempt = 0; attempt < 1000 && camera_host::dequeued_buffers < 60; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (camera_host::dequeued_buffers < 60) return 4;
    camera.StopPreview();
    camera_host::JoinTasks();

    if (snapshots != 8) return 5;
    if (camera_host::sensor_register_reads != (26 + 31) * 4) return 6;
    if (camera_host::sensor_register_writes != 12) return 7;
    if (camera_host::sensor_register_page != 0) return 8;
    if (camera_host::test_pattern_calls != 0) return 9;
    if (camera_host::frame_mappings != 0 || retirement_host::Snapshot().live_tasks != 0) return 10;

    std::cout << "CAMERA_SENSOR_DIAGNOSTICS_ENABLED" << std::endl;
    return 0;
}
