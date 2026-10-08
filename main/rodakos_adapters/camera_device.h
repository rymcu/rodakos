#pragma once

#include <esp_err.h>

namespace rodakos {

class CameraDevice {
public:
    bool IsConfigured() const;
    esp_err_t Acquire();
    esp_err_t Release();

    const char* dev_path() const { return dev_path_; }
    bool acquired() const { return acquired_; }

private:
    bool acquired_ = false;
    bool release_retry_required_ = false;
    const char* dev_path_ = nullptr;
};

}  // namespace rodakos
