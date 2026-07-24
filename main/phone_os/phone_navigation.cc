#include "phone_os/phone_navigation.h"

#include "phone_os/phone_system.h"

bool PhoneNavigation::Launch(std::string_view app_id) {
    return system_.LaunchApp(app_id);
}

bool PhoneNavigation::RefreshTheme() {
    return system_.RefreshTheme();
}

bool PhoneNavigation::ReturnHome() {
    return system_.ReturnHome();
}

bool PhoneNavigation::Lock() {
    return system_.Lock();
}

bool PhoneNavigation::ToggleControlCenter() {
    return system_.ToggleControlCenter();
}

PhoneShellPreferences PhoneNavigation::GetShellPreferences() const {
    return system_.GetShellPreferences();
}

bool PhoneNavigation::SetLockOnBoot(bool enabled) {
    return system_.SetLockOnBoot(enabled);
}

bool PhoneNavigation::SetControlCenterGestureEnabled(bool enabled) {
    return system_.SetControlCenterGestureEnabled(enabled);
}

PhoneAppHostState PhoneNavigation::GetAppHostState() const {
    return system_.GetAppHostState();
}
