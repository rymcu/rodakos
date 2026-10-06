#pragma once
#include <esp_err.h>
namespace rodakos {
class CameraDevice {
public:
    bool IsConfigured() const { return true; }
    esp_err_t Acquire() { return ESP_OK; }
    void Release() {}
    const char* dev_path() const { return "/dev/rodakos-test-camera"; }
};
}
