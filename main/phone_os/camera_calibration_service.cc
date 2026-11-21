#include "phone_os/camera_calibration_service.h"

#include "phone_os/camera_service.h"
#include "phone_os/setup_qr_decoder.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>
#include <new>
#include <string>

namespace rodakos {
namespace {

constexpr const char* TAG = "CameraCalibration";
// quirc_end/quirc_decode need about 10 KiB on Xtensa, and the analyzer adds its own frames. On the
// device 12 KiB left only ~1.5 KiB free, so use 16 KiB; the stack is in PSRAM, not internal RAM.
constexpr uint32_t kTaskStackBytes = 16 * 1024;
// About 4-5 analysed frames per second leaves the CPU to LVGL and the camera task.
constexpr int kFrameIntervalMs = 200;
constexpr int kIdlePollMs = 60;

struct PsramFree {
    void operator()(uint8_t* p) const { heap_caps_free(p); }
};

}  // namespace

CameraCalibrationService::CameraCalibrationService(CameraService* camera) : camera_(camera) {}

CameraCalibrationService::~CameraCalibrationService() {
    generation_.fetch_add(1);
    retirement_owner_.Close();
    retirement_owner_.Drain();
}

CameraCalibrationService::Snapshot CameraCalibrationService::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool CameraCalibrationService::Start() {
    if (camera_ == nullptr) return false;
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return true;  // Already running.
    retirement_.Join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = Snapshot{};
        snapshot_.running = true;
    }
    generation_.fetch_add(1);
    retirement_ = ReserveTaskRetirement(retirement_owner_, TaskBody, this);
    TaskHandle_t handle = nullptr;
    const BaseType_t created = !retirement_ ? pdFAIL
        : xTaskCreateWithCaps(TaskRetirementEntry, "cam_calib", kTaskStackBytes,
                              TaskRetirementContext(retirement_), 3, &handle,
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        if (retirement_) CancelTaskRetirement(retirement_);
        retirement_.Reset();
        busy_.store(false);
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.running = false;
        ESP_LOGW(TAG, "Calibration task could not start");
        return false;
    }
    PublishTaskRetirement(retirement_, handle);
    return true;
}

void CameraCalibrationService::Stop() {
    generation_.fetch_add(1);
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.running = false;
}

void CameraCalibrationService::TaskBody(void* arg) {
    auto* service = static_cast<CameraCalibrationService*>(arg);
    service->Run(service->generation_.load());
    service->busy_.store(false);
}

void CameraCalibrationService::Run(uint32_t generation) {
    auto decoder = std::unique_ptr<SetupQrDecoder>(new (std::nothrow) SetupQrDecoder);
    auto frame = std::unique_ptr<CameraFrame>(new (std::nothrow) CameraFrame);
    auto analyzer = std::unique_ptr<CameraCalibrationAnalyzer>(new (std::nothrow) CameraCalibrationAnalyzer);
    std::unique_ptr<uint8_t, PsramFree> gray;
    size_t gray_size = 0;
    if (decoder == nullptr || frame == nullptr || analyzer == nullptr) {
        ESP_LOGW(TAG, "Calibration out of memory");
        return;
    }
    ESP_LOGI(TAG, "Calibration started: internal_free=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));

    uint32_t last_sequence = 0;
    uint32_t analysed = 0;
    std::string payload;
    while (generation_.load() == generation) {
        // GetLatestFrame copies the whole frame; check the sequence first to skip repeats.
        const CameraState state = camera_->GetState();
        if (!state.has_frame || state.frame_count == last_sequence) {
            vTaskDelay(pdMS_TO_TICKS(kIdlePollMs));
            continue;
        }
        if (!camera_->GetLatestFrame(*frame) || frame->sequence == last_sequence ||
            frame->width <= 0 || frame->height <= 0) {
            vTaskDelay(pdMS_TO_TICKS(kIdlePollMs));
            continue;
        }
        last_sequence = frame->sequence;
        const size_t needed = static_cast<size_t>(frame->width) * frame->height;
        if (needed != gray_size) {
            gray.reset(static_cast<uint8_t*>(heap_caps_malloc(needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
            gray_size = gray != nullptr ? needed : 0;
            if (gray == nullptr) {
                ESP_LOGW(TAG, "Calibration luma buffer unavailable");
                break;
            }
        }
        CameraRgb565ToGray(frame->rgb565.data(), frame->width, frame->height, frame->stride, gray.get());

        CameraChartDetection chart;
        SetupQrDecoder::Location location;
        const CameraChartLayout* layout = nullptr;
        if (decoder->DecodeGray(gray.get(), frame->width, frame->height, frame->width, payload, location) &&
            (layout = FindCameraChartLayout(payload.data(), payload.size())) != nullptr) {
            chart.found = true;
            chart.layout = layout;
            chart.mirrored = location.mirrored;
            for (int i = 0; i < 4; ++i) {
                chart.corners[i] = {static_cast<double>(location.corner_x[i]),
                                    static_cast<double>(location.corner_y[i])};
            }
        }
        const CameraCalibrationResult result = analyzer->Analyze(
            gray.get(), frame->width, frame->height, frame->width, chart, frame->sequence);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation_.load() != generation) break;
            snapshot_.result = result;
            snapshot_.frame_width = frame->width;
            snapshot_.frame_height = frame->height;
            ++snapshot_.revision;
        }
        if (++analysed == 50) {
            ESP_LOGI(TAG, "Calibration running: score=%d best=%d chart=%d mirrored=%d stack_free=%u",
                     result.score, result.best, chart.found ? 1 : 0, chart.mirrored ? 1 : 0,
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        }
        vTaskDelay(pdMS_TO_TICKS(kFrameIntervalMs));
    }
    ESP_LOGI(TAG, "Calibration stopped after %u frames", static_cast<unsigned>(analysed));
}

}  // namespace rodakos
