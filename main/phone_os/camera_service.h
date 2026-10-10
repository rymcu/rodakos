#pragma once

#include <cstdint>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "rodakos_adapters/camera_device.h"
#include "phone_os/camera_signal_diagnostics.h"
#include "task-retirement.h"

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
    enum class PreviewOwner : uint8_t {
        kLocal,
        kRemote,
    };

    using JpegFrameCallback =
        std::function<void(std::vector<uint8_t>&&, uint32_t sequence, int64_t timestamp_us)>;

    explicit CameraService(FileService* file_service);
    ~CameraService();

    bool StartPreview(PreviewOwner owner, int width = 320, int height = 240);
    void StopPreview(PreviewOwner owner);
    // Backward-compatible local preview lease for existing app callers.
    bool StartPreview(int width = 320, int height = 240) {
        return StartPreview(PreviewOwner::kLocal, width, height);
    }
    void StopPreview() { StopPreview(PreviewOwner::kLocal); }
    bool GetLatestFrame(CameraFrame& frame);
    // Encodes the newest RGB565 preview frame as a standalone JPEG buffer.
    // The caller owns the returned bytes and may forward them to a transport.
    bool CaptureJpeg(std::vector<uint8_t>& jpeg, uint32_t* sequence = nullptr,
                     int64_t* timestamp_us = nullptr);
    // Starts a bounded-rate JPEG producer over the already running preview.
    // The callback is invoked outside the service mutex and owns the moved bytes.
    bool StartJpegStream(uint8_t fps, JpegFrameCallback callback);
    void StopJpegStream();
    bool CapturePhoto(std::string& saved_path);
    CameraState GetState() const;
    bool IsAvailable() const;
    std::string last_error() const;

private:
    struct VideoBuffer {
        uint8_t* data = nullptr;
        size_t length = 0;
    };

    static void PreviewTaskEntry(void* arg);
    static void JpegStreamTaskEntry(void* arg);
    void PreviewTask();
    void JpegStreamTask();
    bool OpenStream(int width, int height);
    void CloseStream();
    bool& PreviewLease(PreviewOwner owner);
    bool ShouldStopPreview() const;
    void MarkPreviewStopped();
    void SetError(const std::string& error);
    std::string BuildPhotoPath();

    FileService* file_service_ = nullptr;
    CameraDevice camera_device_;
    CameraSignalDiagnostics signal_diagnostics_;
    std::mutex lifecycle_mutex_;
    std::mutex capture_mutex_;
    SemaphoreHandle_t mutex_ = nullptr;
    TaskRetirementOwner task_owner_;
    // Keep the latest generation after logical completion; a late Stop must
    // still join its complete return. A new Start replaces only this reference.
    TaskRetirementTicket preview_task_;
    TaskRetirementTicket jpeg_stream_task_;
    bool preview_running_ = false;
    bool stop_requested_ = false;
    bool local_preview_lease_ = false;
    bool remote_preview_lease_ = false;
    bool jpeg_stream_running_ = false;
    bool jpeg_stream_stop_requested_ = false;
    // Keep the V4L2 fd, mapped buffers and Board Manager ownership together
    // until STREAMOFF succeeds; closing them after a failed stop strands the
    // lower-level DVP controller and makes a later retry impossible.
    std::atomic_bool streamoff_retry_required_{false};
    // STREAMOFF is valid only after STREAMON completed successfully.
    bool stream_started_ = false;
    uint8_t jpeg_stream_fps_ = 0;
    JpegFrameCallback jpeg_stream_callback_;
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
