#pragma once

#include "phone_os/voice_overlay_model.h"

#include <cstdint>
#include <string>

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
    // The theme changed: restyle the voice overlay with the new colors.
    void RefreshTheme();
    // LVGL task: drop the voice bar at once because a display share starts capturing.
    void HidePrivateContentNow();

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
    static void VoiceTimerCallback(lv_timer_t* timer);
    static void VoiceOverlayClicked(lv_event_t* event);

    void HandleIndevEvent(lv_event_t* event);
    bool RequestMode(PhoneShellMode mode);
    void ApplyMode(PhoneShellMode mode);
    void DestroyOverlay();
    void CreateControlCenter();
    void CreateLockScreen();
    void UpdateLockScreen();
    void QueueOpenSettings();
    void UpdateVoiceOverlay();
    void ApplyVoiceFrame(const rodakos::VoiceOverlayFrame& frame);
    void CreateVoiceOverlay();
    void DestroyVoiceOverlay();
    void StyleVoiceOverlay();
    rodakos::VoiceOverlayContext CollectVoiceContext();
    void UpdateLockNoticeHint();

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
    lv_obj_t* lock_notice_ = nullptr;
    lv_obj_t* lock_notice_label_ = nullptr;
    lv_timer_t* clock_timer_ = nullptr;
    // Bottom voice bar on lv_layer_top(): turn progress before a reply, then reply captions.
    // It lives below the Lock Screen and Control Center objects, which are moved to the front.
    lv_obj_t* voice_overlay_ = nullptr;
    lv_obj_t* voice_label_ = nullptr;
    lv_timer_t* voice_timer_ = nullptr;
    rodakos::VoiceOverlayModel voice_model_;
    rodakos::VoiceOverlayFrame voice_frame_;
    rodakos::VoiceTurnProgressSnapshot voice_snapshot_;
    uint32_t voice_revision_ = UINT32_MAX;
    lv_point_t gesture_start_ = {0, 0};
    bool initialized_ = false;
    bool gestures_available_ = false;
    bool gesture_tracking_ = false;
    bool suppress_clicks_ = false;
    bool mode_change_queued_ = false;
    bool navigation_queued_ = false;
};
