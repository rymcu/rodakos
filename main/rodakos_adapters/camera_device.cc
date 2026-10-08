#include "rodakos_adapters/camera_device.h"

#include "sdkconfig.h"

#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
#include <dev_camera.h>
#include <esp_board_manager.h>
#include <esp_log.h>
#endif

namespace rodakos {
namespace {
constexpr const char* TAG = "CameraDevice";
constexpr const char* kCameraDeviceName = "camera";
}

bool CameraDevice::IsConfigured() const {
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    return esp_board_manager_check_name(kCameraDeviceName);
#else
    return false;
#endif
}

esp_err_t CameraDevice::Acquire() {
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    if (acquired_ && !release_retry_required_) {
        return ESP_OK;
    }

    if (release_retry_required_) {
        const esp_err_t release_ret = Release();
        if (release_ret != ESP_OK) {
            return release_ret;
        }
    }

    esp_err_t ret = esp_board_manager_init_device_by_name(kCameraDeviceName);
    if (ret != ESP_OK) {
        return ret;
    }
    // A successful init owns a board reference even if the handle lookup fails.
    // Release() must retain that ownership when cleanup needs another attempt.
    acquired_ = true;

    dev_camera_handle_t* camera_handle = nullptr;
    ret = esp_board_manager_get_device_handle(kCameraDeviceName,
                                              reinterpret_cast<void**>(&camera_handle));
    if (ret != ESP_OK || camera_handle == nullptr || camera_handle->dev_path == nullptr) {
        const esp_err_t release_ret = Release();
        if (release_ret != ESP_OK) {
            return release_ret;
        }
        return ret != ESP_OK ? ret : ESP_ERR_NOT_FOUND;
    }

    dev_path_ = camera_handle->dev_path;
    acquired_ = true;
    release_retry_required_ = false;
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t CameraDevice::Release() {
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    if (acquired_) {
        const esp_err_t ret = esp_board_manager_deinit_device_by_name(kCameraDeviceName);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Camera device release failed: %s", esp_err_to_name(ret));
            release_retry_required_ = true;
            return ret;
        }
    }
#endif
    acquired_ = false;
    release_retry_required_ = false;
    dev_path_ = nullptr;
    return ESP_OK;
}

}  // namespace rodakos
