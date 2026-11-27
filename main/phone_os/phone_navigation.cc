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
    child_app_.clear();
    caller_app_.clear();
    requested_page_.clear();
    return system_.LaunchApp(app_id);
}

bool PhoneNavigation::LaunchFrom(std::string_view app_id, std::string_view caller, std::string_view page) {
    child_app_.assign(app_id);
    caller_app_.assign(caller);
    // Set before the launch: the child's OnCreate runs inside LaunchApp and reads it there.
    requested_page_.assign(page);
    if (!system_.LaunchApp(app_id)) {
        child_app_.clear();
        caller_app_.clear();
        requested_page_.clear();
        return false;
    }
    return true;
}

std::string PhoneNavigation::TakeRequestedPage() {
    std::string page = std::move(requested_page_);
    requested_page_.clear();
    return page;
}

bool PhoneNavigation::ReturnToCaller() {
    const std::string caller = caller_app_;
    if (caller.empty() || system_.GetAppHostState().current_app_id != child_app_) {
        return ReturnHome();
    }
    return Launch(caller) || ReturnHome();
}

bool PhoneNavigation::RefreshTheme() {
    return system_.RefreshTheme();
}

bool PhoneNavigation::ReturnHome() {
    child_app_.clear();
    caller_app_.clear();
    requested_page_.clear();
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
