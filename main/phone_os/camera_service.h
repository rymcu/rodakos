#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace rodakos {

class FileService;

struct CameraFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<uint8_t> rgb565;
    int64_t timestamp_us = 0;
    uint32_t sequence = 0;
};

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
    explicit CameraService(FileService* file_service);
    ~CameraService();

    bool StartPreview(int width = 320, int height = 240);
    void StopPreview();
    bool GetLatestFrame(CameraFrame& frame);
    bool CapturePhoto(std::string& saved_path);
    CameraState GetState() const;
    bool IsAvailable() const;
    const char* last_error() const { return last_error_.c_str(); }

private:
    struct VideoBuffer {
        uint8_t* data = nullptr;
        size_t length = 0;
    };

    static void PreviewTaskEntry(void* arg);
    void PreviewTask();
    bool OpenStream(int width, int height);
    void CloseStream();
    bool ShouldStopPreview() const;
    void MarkPreviewStopped();
    void SetError(const std::string& error);
    std::string BuildPhotoPath();

    FileService* file_service_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t preview_task_ = nullptr;
    bool preview_running_ = false;
    bool stop_requested_ = false;
    bool camera_ref_acquired_ = false;
    int fd_ = -1;
    int active_width_ = 0;
    int active_height_ = 0;
    int active_stride_ = 0;
    uint32_t active_pixelformat_ = 0;
    std::vector<VideoBuffer> buffers_;
    CameraFrame latest_frame_;
    bool has_frame_ = false;
    uint32_t frame_count_ = 0;
    std::string last_saved_path_;
    std::string last_error_;
};

}  // namespace rodakos
