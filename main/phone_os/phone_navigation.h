#pragma once

#include "phone_os/phone_app_host.h"
#include "phone_os/phone_shell.h"

#include <string_view>

class PhoneSystem;

class PhoneNavigation {
public:
    explicit PhoneNavigation(PhoneSystem& system) : system_(system) {}

    bool Launch(std::string_view app_id);
    bool RefreshTheme();
    bool ReturnHome();
    bool Lock();
    bool ToggleControlCenter();
    PhoneShellPreferences GetShellPreferences() const;
    bool SetLockOnBoot(bool enabled);
    bool SetControlCenterGestureEnabled(bool enabled);
    PhoneAppHostState GetAppHostState() const;

private:
    PhoneSystem& system_;
};
