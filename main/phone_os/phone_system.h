#pragma once

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_host.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"

#include "settings.h"

#include <string_view>

class PhoneUi;

class PhoneSystem {
public:
    PhoneSystem(PhoneUi& ui, PhoneServices& services);

    void RegisterBuiltInApps();
    bool Start();
    bool LaunchApp(std::string_view app_id);
    bool ReturnHome();

    PhoneAppRegistry& registry() { return registry_; }
    PhoneNavigation& navigation() { return navigation_; }
    PhoneUi& ui() { return ui_; }
    Settings& settings() { return settings_; }

private:
    PhoneUi& ui_;
    PhoneAppRegistry registry_;
    PhoneAppHost host_;
    PhoneNavigation navigation_;
    PhoneServices& services_;
    Settings settings_;
    PhoneAppContext context_;
};
