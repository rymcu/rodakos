#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "lvgl.h"
namespace rodakos_test::display_service_host {
constexpr size_t kFrameBytes = 320 * 240 * 2;
constexpr size_t kRgbBytes = 320 * 240 * 3;
constexpr size_t kJpegScratchBytes = 100 * 1024;
constexpr size_t kJpegBytes = 4096;
struct Resources {
    size_t buffers = 0, bytes = 0, peak_bytes = 0, encoders = 0;
    size_t opens = 0, closes = 0, processes = 0;
    size_t last_output_capacity = 0;
    uint32_t first_rgb888 = 0, middle_rgb888 = 0, last_rgb888 = 0;
    size_t new_failures = 0, heap_failures = 0;
    size_t event_adds = 0, event_removes = 0, event_ops_without_lvgl_lock = 0;
};
void Reset();
void JoinTasks();
void FailNew(size_t bytes, size_t nth = 1, size_t count = 1, bool worker_only = false);
void FailHeap(size_t bytes, size_t count = 2, bool after_encoder_open = false);
void ClearFailures();
void FailEncoderOpen(bool fail);
void FailEncoderProcess(bool fail);
void SetEncodedSize(size_t bytes);
void AllowTaskCreation(bool allow);
void AllowAsync(bool allow);
Resources Snapshot();
std::vector<int64_t> NewFailureTimes();
std::vector<int64_t> ProcessTimes();
size_t PendingAsync();
void Flush(lv_display_t& display, uint8_t* pixels, const lv_area_t& area, bool last = true);
}
