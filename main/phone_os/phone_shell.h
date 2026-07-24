#pragma once

#include <cstdint>

#include <lvgl.h>

class PhoneServices;
class PhoneSystem;
class PhoneUi;

enum class PhoneShellMode : uint8_t {
    kNone,
    kControlCenter,
    kLocked,
};

struct PhoneShellPreferences {
    bool lock_on_boot = false;
    bool control_center_gesture_enabled = true;
};

class PhoneShell {
public:
    PhoneShell(PhoneSystem& system, PhoneUi& ui, PhoneServices& services);

    bool Initialize();
    bool PrepareNavigation();
    bool Lock();
    bool ToggleControlCenter();
    PhoneShellPreferences preferences() const { return preferences_; }
    bool SetLockOnBoot(bool enabled);
    bool SetControlCenterGestureEnabled(bool enabled);

    bool locked() const { return mode_ == PhoneShellMode::kLocked; }
    bool control_center_open() const { return mode_ == PhoneShellMode::kControlCenter; }
    PhoneShellMode mode() const { return mode_; }

private:
    static void IndevEventCallback(lv_event_t* event);
    static void ApplyPendingModeAsync(void* user_data);
    static void OpenSettingsCallback(lv_event_t* event);
    static void OpenSettingsAsync(void* user_data);
    static void CloseCallback(lv_event_t* event);
    static void LockCallback(lv_event_t* event);
    static void BrightnessChangedCallback(lv_event_t* event);
    static void BrightnessReleasedCallback(lv_event_t* event);
    static void ClockTimerCallback(lv_timer_t* timer);

    void HandleIndevEvent(lv_event_t* event);
    bool RequestMode(PhoneShellMode mode);
    void ApplyMode(PhoneShellMode mode);
    void DestroyOverlay();
    void CreateControlCenter();
    void CreateLockScreen();
    void UpdateLockScreen();
    void QueueOpenSettings();

    PhoneSystem& system_;
    PhoneUi& ui_;
    PhoneServices& services_;
    PhoneShellPreferences preferences_;
    PhoneShellMode mode_ = PhoneShellMode::kNone;
    PhoneShellMode pending_mode_ = PhoneShellMode::kNone;
    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* clock_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* lock_wifi_label_ = nullptr;
    lv_timer_t* clock_timer_ = nullptr;
    lv_point_t gesture_start_ = {0, 0};
    bool initialized_ = false;
    bool gestures_available_ = false;
    bool gesture_tracking_ = false;
    bool suppress_clicks_ = false;
    bool mode_change_queued_ = false;
    bool navigation_queued_ = false;
};
