#pragma once
#include <esp_err.h>
#include "../../host_runtime.h"
namespace rodakos {
class CameraDevice {
public:
    bool IsConfigured() const { return camera_host::IsCameraConfigured(); }
    esp_err_t Acquire() { return ESP_OK; }
    void Release() {}
    const char* dev_path() const { return "/dev/rodakos-test-camera"; }
};
}
