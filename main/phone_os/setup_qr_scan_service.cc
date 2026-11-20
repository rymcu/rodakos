#include "phone_os/setup_qr_scan_service.h"

#include "phone_os/audio_focus_service.h"
#include "phone_os/camera_service.h"
#include "phone_os/device_cloud_config.h"
#include "phone_os/setup_qr_decoder.h"
#include "rodakos_adapters/wifi_adapter.h"

#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>

#include <memory>
#include <new>

namespace rodakos {
namespace {

constexpr const char* TAG = "SetupQrScan";
// quirc_end/quirc_decode need about 10 KiB on Xtensa; the stack is in PSRAM, not internal RAM.
constexpr uint32_t kTaskStackBytes = 12 * 1024;
constexpr int kFramePollMs = 150;
constexpr int64_t kScanTimeoutMs = 90 * 1000;
constexpr int kCameraFirstFrameTimeoutMs = 5000;

int64_t NowMs() { return esp_timer_get_time() / 1000; }

std::string DisplayServer(const std::string& bootstrap_url) {
    const std::string scheme = "https://";
    if (bootstrap_url.compare(0, scheme.size(), scheme) != 0) return {};
    const size_t end = bootstrap_url.find('/', scheme.size());
    return bootstrap_url.substr(scheme.size(), end == std::string::npos ? std::string::npos
                                                                       : end - scheme.size());
}

}  // namespace

SetupQrScanService::SetupQrScanService(CameraService* camera, AudioFocusService* audio_focus,
                                       WiFiAdapter* wifi, DeviceCloudConfigService& cloud)
    : camera_(camera), audio_focus_(audio_focus), wifi_(wifi), cloud_(cloud) {}

SetupQrScanService::~SetupQrScanService() {
    cancel_generation_.fetch_add(1);
    retirement_owner_.Close();
    retirement_owner_.Drain();
    ReleaseResources();
}

bool SetupQrScanService::Available() const { return camera_ != nullptr && camera_->IsAvailable(); }

void SetupQrScanService::SetServerChangedCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    server_changed_callback_ = std::move(callback);
}

SetupQrScanSnapshot SetupQrScanService::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool SetupQrScanService::StartScan() { return StartJob(Job::kScan); }

bool SetupQrScanService::Confirm() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (snapshot_.phase != SetupQrScanPhase::kConfirm) return false;
    }
    return StartJob(Job::kSave);
}

void SetupQrScanService::Cancel() {
    cancel_generation_.fetch_add(1);
    // The worker notices the new generation within one poll and releases the camera itself.
    std::lock_guard<std::mutex> lock(mutex_);
    payload_ = {};
    const uint32_t revision = snapshot_.revision + 1;
    snapshot_ = SetupQrScanSnapshot{};
    snapshot_.revision = revision;
}

bool SetupQrScanService::StartJob(Job job) {
    if (job == Job::kScan && !Available()) return false;
    // Never take over a preview lease that the Camera app or a remote viewer already holds.
    if (job == Job::kScan && camera_->GetState().preview_running) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.phase = SetupQrScanPhase::kFailed;
        snapshot_.code = "qr_camera_busy";
        ++snapshot_.revision;
        return false;
    }
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return false;
    retirement_.Join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job_ = job;
        job_generation_ = cancel_generation_.load();
        if (job == Job::kScan) {
            payload_ = {};
            const uint32_t revision = snapshot_.revision;
            snapshot_ = SetupQrScanSnapshot{};
            snapshot_.revision = revision;
            snapshot_.phase = SetupQrScanPhase::kStarting;
        } else {
            snapshot_.phase = SetupQrScanPhase::kSaving;
        }
        snapshot_.code.clear();
        ++snapshot_.revision;
    }
    retirement_ = ReserveTaskRetirement(retirement_owner_, TaskBody, this);
    TaskHandle_t handle = nullptr;
    const BaseType_t created = !retirement_ ? pdFAIL
        : xTaskCreateWithCaps(TaskRetirementEntry, "setup_qr", kTaskStackBytes,
                              TaskRetirementContext(retirement_), 4, &handle,
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        if (retirement_) CancelTaskRetirement(retirement_);
        retirement_.Reset();
        busy_.store(false);
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.phase = SetupQrScanPhase::kFailed;
        snapshot_.code = "qr_task_start_failed";
        ++snapshot_.revision;
        return false;
    }
    PublishTaskRetirement(retirement_, handle);
    return true;
}

void SetupQrScanService::TaskBody(void* arg) {
    auto* service = static_cast<SetupQrScanService*>(arg);
    Job job;
    {
        std::lock_guard<std::mutex> lock(service->mutex_);
        job = service->job_;
    }
    if (job == Job::kScan) service->RunScan();
    else service->RunSave();
    service->busy_.store(false);
}

bool SetupQrScanService::Cancelled(uint32_t generation) const {
    return cancel_generation_.load() != generation;
}

void SetupQrScanService::Publish(uint32_t generation, SetupQrScanPhase phase, std::string code) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancel_generation_.load() != generation) return;
    snapshot_.phase = phase;
    snapshot_.code = std::move(code);
    ++snapshot_.revision;
}

void SetupQrScanService::ReleaseResources() {
    if (camera_started_ && camera_ != nullptr) camera_->StopPreview(CameraService::PreviewOwner::kLocal);
    camera_started_ = false;
    if (audio_focus_token_ != 0 && audio_focus_ != nullptr) audio_focus_->ReleaseFocus(audio_focus_token_);
    audio_focus_token_ = 0;
}

void SetupQrScanService::RunScan() {
    uint32_t generation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation = job_generation_;
    }
    // Same order as the Camera app: release the codec first to give the DVP driver DMA room.
    if (audio_focus_ != nullptr) {
        AudioFocusRequest request;
        request.owner = "setup_qr";
        request.gain = AudioFocusGain::kExclusive;
        request.resume_on_release = false;
        request.release_playback_hardware = true;
        if (!audio_focus_->RequestFocus(request, audio_focus_token_)) {
            audio_focus_token_ = 0;
            ESP_LOGW(TAG, "Exclusive audio focus unavailable; starting camera anyway");
        }
    }
    if (Cancelled(generation)) {
        ReleaseResources();
        return;
    }
    camera_started_ = camera_->StartPreview(CameraService::PreviewOwner::kLocal);
    if (!camera_started_) {
        ESP_LOGW(TAG, "Camera preview failed: %s", camera_->last_error().c_str());
        ReleaseResources();
        Publish(generation, SetupQrScanPhase::kFailed, "qr_camera_unavailable");
        return;
    }
    Publish(generation, SetupQrScanPhase::kScanning);

    auto decoder = std::unique_ptr<SetupQrDecoder>(new (std::nothrow) SetupQrDecoder);
    auto frame = std::unique_ptr<CameraFrame>(new (std::nothrow) CameraFrame);
    if (decoder == nullptr || frame == nullptr) {
        ReleaseResources();
        Publish(generation, SetupQrScanPhase::kFailed, "qr_out_of_memory");
        return;
    }

    const int64_t started_ms = NowMs();
    uint32_t last_sequence = 0;
    std::string text;
    bool found = false;
    while (!Cancelled(generation)) {
        const int64_t elapsed_ms = NowMs() - started_ms;
        if (elapsed_ms > kScanTimeoutMs) break;
        if (!camera_->GetLatestFrame(*frame)) {
            const CameraState state = camera_->GetState();
            if ((!state.preview_running && !state.last_error.empty()) ||
                (last_sequence == 0 && elapsed_ms > kCameraFirstFrameTimeoutMs)) {
                ESP_LOGW(TAG, "Camera stopped delivering frames: %s", state.last_error.c_str());
                ReleaseResources();
                Publish(generation, SetupQrScanPhase::kFailed, "qr_camera_unavailable");
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(kFramePollMs));
            continue;
        }
        if (frame->sequence == last_sequence) {
            vTaskDelay(pdMS_TO_TICKS(kFramePollMs));
            continue;
        }
        last_sequence = frame->sequence;
        found = decoder->DecodeRgb565(frame->rgb565.data(), frame->width, frame->height,
                                      frame->stride, text);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cancel_generation_.load() == generation) ++snapshot_.frames_scanned;
        }
        if (found) break;
        // Leave CPU time to LVGL and the camera task between decodes.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    // Stop the camera before any parsing or NVS work, so it never overlaps WiFi/TLS memory use.
    ReleaseResources();
    decoder.reset();
    frame.reset();
    if (Cancelled(generation)) return;
    if (!found) {
        Publish(generation, SetupQrScanPhase::kFailed, "qr_scan_timeout");
        return;
    }

    SetupQrPayload payload;
    const SetupQrError error = ParseSetupQr(text, payload);
    text.assign(text.size(), '\0');
    if (error != SetupQrError::kNone) {
        ESP_LOGW(TAG, "QR code rejected: %s", SetupQrErrorCode(error));
        Publish(generation, SetupQrScanPhase::kFailed, SetupQrErrorCode(error));
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancel_generation_.load() != generation) return;
    snapshot_.kind = payload.kind;
    snapshot_.ssid = payload.ssid;
    snapshot_.open_network = payload.open_network;
    snapshot_.server = DisplayServer(payload.bootstrap_url);
    snapshot_.phase = SetupQrScanPhase::kConfirm;
    snapshot_.code.clear();
    ++snapshot_.revision;
    payload_ = std::move(payload);
    ESP_LOGI(TAG, "QR code accepted: kind=%s", payload_.kind == SetupQrKind::kRodak ? "rodak" : "wifi");
}

void SetupQrScanService::RunSave() {
    uint32_t generation;
    SetupQrPayload payload;
    std::function<void()> server_changed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation = job_generation_;
        payload = std::move(payload_);
        payload_ = {};
        server_changed = server_changed_callback_;
    }
    if (Cancelled(generation)) return;

    SetupQrApplyStatus status = SetupQrApplyStatus::kStorageFailed;
    RunWithInternalStack([&]() {
        DeviceSetupQrProvisioningStore store(cloud_);
        status = ApplySetupQrProvisioning(payload, store);
    });
    ESP_LOGI(TAG, "QR provisioning save: %s", SetupQrApplyStatusCode(status));

    if (IsSetupQrApplySuccess(status) && wifi_ != nullptr) {
        // Same as USB provisioning: a fresh association also starts the cloud client on IP.
        bool connect_started = false;
        if (wifi_->DisconnectAndWait(2000)) {
            for (int attempt = 0; attempt < 3 && !connect_started; ++attempt) {
                connect_started = wifi_->Connect(payload.ssid, payload.password, [](WiFiStatus) {});
            }
        }
        if (!connect_started) ESP_LOGW(TAG, "WiFi reconnect did not start; credentials remain saved");
    }
    payload.password.assign(payload.password.size(), '\0');
    payload = {};

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancel_generation_.load() == generation) {
            snapshot_.result = status;
            snapshot_.phase = IsSetupQrApplySuccess(status) ? SetupQrScanPhase::kCompleted
                                                            : SetupQrScanPhase::kFailed;
            snapshot_.code = SetupQrApplyStatusCode(status);
            ++snapshot_.revision;
        }
    }
    // The bootstrap URL changed: restart the cloud client so no old credentials cross over.
    if (status == SetupQrApplyStatus::kServerChanged && server_changed) server_changed();
}

void SetupQrScanService::RunWithInternalStack(const std::function<void()>& work) {
    if (esp_ptr_internal(esp_cpu_get_sp()) || internal_work_task_ == nullptr) {
        work();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(internal_work_mutex_);
        internal_work_ = &work;
        internal_work_waiter_ = xTaskGetCurrentTaskHandle();
    }
    xTaskNotifyGive(internal_work_task_);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

void SetupQrScanService::PumpInternalStackWork() {
    const std::function<void()>* work = nullptr;
    TaskHandle_t waiter = nullptr;
    {
        std::lock_guard<std::mutex> lock(internal_work_mutex_);
        work = internal_work_;
        waiter = internal_work_waiter_;
        internal_work_ = nullptr;
        internal_work_waiter_ = nullptr;
    }
    if (work == nullptr) return;
    (*work)();
    xTaskNotifyGive(waiter);
}

}  // namespace rodakos
