#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace camera_host {
extern std::atomic<bool> fail_mount, fail_directory, fail_write, fail_flush, fail_close;
extern std::atomic<bool> fail_encoder_open, fail_encoder_process, fail_allocation, empty_encoded;
extern std::atomic<bool> collide_on_create;
extern std::atomic<bool> fail_dequeue;
extern std::atomic<bool> pause_frames, frames_paused;
extern std::atomic<unsigned> encoder_handles, frame_mappings;
extern std::atomic<size_t> preview_frame_bytes;
extern std::atomic<unsigned> new_failures, aligned_buffers, dequeued_buffers, requeued_buffers;
enum class AllocationThread { kCaller, kPreview, kJpeg };
void FailNew(size_t bytes, size_t nth, size_t count, AllocationThread thread);
void ClearNewFailures();
void* AllocateAligned(size_t alignment, size_t bytes);
void FreeAligned(void* pointer);
extern std::function<void()> write_hook;
extern std::function<void()> preview_state_query_hook;
extern std::string collision_path;
bool IsCameraConfigured();
void Reset(const std::string& mount_path);
void JoinTasks();
std::vector<uint8_t> EncodedBytes();
}
