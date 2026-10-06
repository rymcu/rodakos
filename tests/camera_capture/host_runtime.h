#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace camera_host {
extern std::atomic<bool> fail_mount, fail_directory, fail_write, fail_flush, fail_close;
extern std::atomic<bool> fail_encoder_open, fail_encoder_process, fail_allocation, empty_encoded;
extern std::atomic<bool> collide_on_create;
extern std::atomic<unsigned> encoder_handles, frame_mappings;
extern std::function<void()> write_hook;
extern std::string collision_path;
void Reset(const std::string& mount_path);
void JoinTasks();
std::vector<uint8_t> EncodedBytes();
}
