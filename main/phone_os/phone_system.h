#pragma once

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_host.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/phone_shell.h"

#include "settings.h"

#include <string_view>

class PhoneUi;

class PhoneSystem {
public:
    PhoneSystem(PhoneUi& ui, PhoneServices& services);
    ~PhoneSystem();

    void RegisterBuiltInApps();
    bool Start();
    bool LaunchApp(std::string_view app_id);
    bool RefreshTheme();
    bool ReturnHome();
    bool Back();
    bool Lock();
    bool ToggleControlCenter();
    PhoneShellPreferences GetShellPreferences() const { return shell_.preferences(); }
    bool SetLockOnBoot(bool enabled) { return shell_.SetLockOnBoot(enabled); }
    bool SetControlCenterGestureEnabled(bool enabled) {
        return shell_.SetControlCenterGestureEnabled(enabled);
    }

    PhoneAppRegistry& registry() { return registry_; }
    PhoneNavigation& navigation() { return navigation_; }
    PhoneUi& ui() { return ui_; }
    Settings& settings() { return settings_; }
    PhoneAppHostState GetAppHostState() const { return host_.GetState(); }
    // The running app, for Shell overlay arbitration only. Null between apps.
    const PhoneApp* current_app() const { return host_.current_app(); }
    // A display share is about to capture its first frame (LVGL task, lock held): hide the
    // voice bar and let the current app hide private text before that frame is drawn.
    void HidePrivateContentNow();

private:
    PhoneUi& ui_;
    PhoneAppRegistry registry_;
    PhoneAppHost host_;
    PhoneNavigation navigation_;
    PhoneServices& services_;
    PhoneShell shell_;
    Settings settings_;
    PhoneAppContext context_;
};
