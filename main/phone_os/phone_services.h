#pragma once

class BacklightAdapter;
class WiFiAdapter;

class PhoneServices {
public:
    void SetBacklight(BacklightAdapter* backlight) { backlight_ = backlight; }
    BacklightAdapter* backlight() { return backlight_; }

    void SetWiFi(WiFiAdapter* wifi) { wifi_ = wifi; }
    WiFiAdapter* wifi() { return wifi_; }

private:
    BacklightAdapter* backlight_ = nullptr;
    WiFiAdapter* wifi_ = nullptr;
};
