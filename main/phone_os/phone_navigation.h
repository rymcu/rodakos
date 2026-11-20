#pragma once

#include "phone_os/phone_app_host.h"
#include "phone_os/phone_shell.h"
#include "phone_os/deferred_navigation.h"

#include <string>
#include <string_view>

class PhoneSystem;

class PhoneNavigation {
public:
    explicit PhoneNavigation(PhoneSystem& system) : system_(system) {}

    bool Launch(std::string_view app_id);
    // Opens `app_id` as a sub-page of `caller` (for example Settings -> 主题). The child's back
    // action then returns to the caller instead of the desktop. Any other launch forgets this.
    bool LaunchFrom(std::string_view app_id, std::string_view caller);
    // Back from an app opened with LaunchFrom: relaunches the caller, otherwise the desktop.
    bool ReturnToCaller();
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
    std::string child_app_;
    std::string caller_app_;
};
