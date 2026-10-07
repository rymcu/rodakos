#include "phone_os/phone_navigation.h"

#include "phone_os/phone_system.h"
#include "phone_ui/phone_ui.h"

bool PhoneNavigation::InitializeDeferred() {
    PhoneUiLock lock(system_.ui());
    return lock.locked() && deferred_.Initialize([](void* context, std::string_view id, bool home) {
        auto& navigation = *static_cast<PhoneNavigation*>(context);
        return home ? navigation.ReturnHome() : navigation.Launch(id);
    }, this);
}

void PhoneNavigation::CloseDeferred() {
    // Shutdown must synchronize with any active timer dispatch before the host,
    // shell or app context is destroyed. A timed failure cannot safely proceed.
    PhoneUiLock lock(system_.ui(), 0);
    deferred_.Close();
}

bool PhoneNavigation::RequestLaunch(std::string_view alias,
                                    DeferredNavigation::Completion completion, void* context) {
    if (alias.empty() || alias.size() > DeferredNavigation::kMaxAppIdBytes ||
        alias.find('\0') != std::string_view::npos) return false;
    const auto* descriptor = system_.registry().ResolveAlias(alias);
    return descriptor != nullptr && deferred_.Enqueue(descriptor->id, false, completion, context);
}

bool PhoneNavigation::RequestHome() {
    return deferred_.Enqueue({}, true);
}

bool PhoneNavigation::Launch(std::string_view app_id) {
    return system_.LaunchApp(app_id);
}

bool PhoneNavigation::RefreshTheme() {
    return system_.RefreshTheme();
}

bool PhoneNavigation::ReturnHome() {
    return system_.ReturnHome();
}

bool PhoneNavigation::Back() {
    return system_.Back();
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
