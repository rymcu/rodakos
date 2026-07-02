#pragma once

#include "phone_ui/phone_theme.h"

#include <cstdint>
#include <string>

#include <lvgl.h>

class PhoneUi {
public:
    PhoneUi(int width, int height);

    bool Lock(int timeout_ms = 1000);
    void Unlock();
    lv_obj_t* screen() const;
    int width() const { return width_; }
    int height() const { return height_; }
    const PhoneTheme& theme() const { return theme_; }

    void SetThemeName(const std::string& name);
    const std::string& theme_name() const { return theme_name_; }
    uint32_t theme_revision() const { return theme_revision_; }
    void ShowToast(const char* message, int duration_ms = 1800);
    void ShowToastUnlocked(const char* message, int duration_ms = 1800);

private:
    int width_;
    int height_;
    std::string theme_name_ = "dark";
    PhoneTheme theme_;
    uint32_t theme_revision_ = 0;
    lv_obj_t* toast_ = nullptr;
    lv_timer_t* toast_timer_ = nullptr;
};

class PhoneUiLock {
public:
    explicit PhoneUiLock(PhoneUi& ui, int timeout_ms = 1000) : ui_(ui), locked_(ui.Lock(timeout_ms)) {}
    ~PhoneUiLock() {
        if (locked_) {
            ui_.Unlock();
        }
    }
    bool locked() const { return locked_; }

private:
    PhoneUi& ui_;
    bool locked_ = false;
};
