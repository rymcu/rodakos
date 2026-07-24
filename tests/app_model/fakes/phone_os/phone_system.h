#pragma once

#include "phone_os/phone_app_host.h"
#include "phone_os/phone_shell.h"

#include <string>
#include <string_view>

class PhoneSystem {
public:
    bool LaunchApp(std::string_view app_id) {
        ++launch_calls;
        launched_app_id.assign(app_id.data(), app_id.size());
        return launch_result;
    }

    bool RefreshTheme() {
        ++refresh_theme_calls;
        return refresh_theme_result;
    }

    bool ReturnHome() {
        ++return_home_calls;
        return return_home_result;
    }

    bool Lock() {
        ++lock_calls;
        return lock_result;
    }

    bool ToggleControlCenter() {
        ++toggle_control_center_calls;
        return toggle_control_center_result;
    }

    PhoneShellPreferences GetShellPreferences() const {
        ++get_shell_preferences_calls;
        return shell_preferences;
    }

    bool SetLockOnBoot(bool enabled) {
        ++set_lock_on_boot_calls;
        lock_on_boot_value = enabled;
        return set_lock_on_boot_result;
    }

    bool SetControlCenterGestureEnabled(bool enabled) {
        ++set_control_center_gesture_calls;
        control_center_gesture_value = enabled;
        return set_control_center_gesture_result;
    }

    PhoneAppHostState GetAppHostState() const {
        ++get_app_host_state_calls;
        return app_host_state;
    }

    bool launch_result = true;
    bool refresh_theme_result = true;
    bool return_home_result = true;
    bool lock_result = true;
    bool toggle_control_center_result = true;
    bool set_lock_on_boot_result = true;
    bool set_control_center_gesture_result = true;
    std::string launched_app_id;
    PhoneShellPreferences shell_preferences;
    PhoneAppHostState app_host_state;
    bool lock_on_boot_value = false;
    bool control_center_gesture_value = true;
    int launch_calls = 0;
    int refresh_theme_calls = 0;
    int return_home_calls = 0;
    int lock_calls = 0;
    int toggle_control_center_calls = 0;
    mutable int get_shell_preferences_calls = 0;
    int set_lock_on_boot_calls = 0;
    int set_control_center_gesture_calls = 0;
    mutable int get_app_host_state_calls = 0;
};
