#pragma once

class BacklightAdapter;
class WiFiAdapter;

namespace rodakos {
class FileService;
}

class PhoneServices {
public:
    void SetBacklight(BacklightAdapter* backlight) { backlight_ = backlight; }
    BacklightAdapter* backlight() { return backlight_; }

    void SetWiFi(WiFiAdapter* wifi) { wifi_ = wifi; }
    WiFiAdapter* wifi() { return wifi_; }

    void SetFileService(rodakos::FileService* file_service) { file_service_ = file_service; }
    rodakos::FileService* file_service() { return file_service_; }

private:
    BacklightAdapter* backlight_ = nullptr;
    WiFiAdapter* wifi_ = nullptr;
    rodakos::FileService* file_service_ = nullptr;
};
