#include "host_runtime.h"
#include "../task_retirement/task_retirement_host.h"
#include "phone_os/camera_service.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2 || (std::strcmp(argv[1], "preview") != 0 &&
                      std::strcmp(argv[1], "jpeg") != 0)) return 2;
    camera_host::Reset("");
    retirement_host::RejectCleanupTask(true);
    rodakos::CameraService camera(nullptr);
    if (!camera.StartPreview(2, 2)) return 3;
    bool ready = false;
    for (unsigned attempt = 0; attempt < 1000 && !ready; ++attempt) {
        ready = camera.GetState().has_frame;
        if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!ready) return 4;
    std::cout << "CAMERA_WORKER_STARTED preview" << std::endl;
    if (std::strcmp(argv[1], "jpeg") == 0) {
        std::atomic<bool> delivered{false};
        if (!camera.StartJpegStream(30, [&](std::vector<uint8_t>&&, uint32_t, int64_t) {
                delivered = true;
            })) return 5;
        for (unsigned attempt = 0; attempt < 1000 && !delivered; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!delivered) return 6;
        std::cout << "CAMERA_WORKER_STARTED jpeg" << std::endl;
        camera.StopJpegStream();
    }
    camera.StopPreview();
    camera_host::JoinTasks();
    const auto resources = retirement_host::Snapshot();
    if (resources.live_tasks != 0 || resources.live_task_buffers != 0 ||
        resources.cleanup_create_attempts != 0) return 7;
    std::cout << "CAMERA_WORKER_RECLAIMED " << argv[1] << std::endl;
    return 0;
}
