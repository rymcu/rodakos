#pragma once

#include "phone_os/phone_app_host.h"
#include "phone_os/phone_shell.h"
#include "phone_os/deferred_navigation.h"

#include <string_view>

class PhoneSystem;

class PhoneNavigation {
public:
    explicit PhoneNavigation(PhoneSystem& system) : system_(system) {}

    bool Launch(std::string_view app_id);
    bool RefreshTheme();
    bool ReturnHome();
    bool RequestLaunch(std::string_view alias, DeferredNavigation::Completion completion = nullptr,
                       void* context = nullptr);
    bool RequestHome();
    bool InitializeDeferred();
    void CloseDeferred();
    bool Back();
    bool Lock();
    bool ToggleControlCenter();
    PhoneShellPreferences GetShellPreferences() const;
    bool SetLockOnBoot(bool enabled);
    bool SetControlCenterGestureEnabled(bool enabled);
    PhoneAppHostState GetAppHostState() const;

private:
    PhoneSystem& system_;
    DeferredNavigation deferred_;
};
