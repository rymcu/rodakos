#pragma once

class PhoneNavigation;
class PhoneAppRegistry;
class PhoneServices;
class PhoneUi;
class Settings;

class PhoneAppContext {
public:
    PhoneAppContext(PhoneUi& ui,
                    PhoneNavigation& navigation,
                    PhoneAppRegistry& registry,
                    PhoneServices& services,
                    Settings& settings)
        : ui_(ui), navigation_(navigation), registry_(registry), services_(services), settings_(settings) {}

    PhoneUi& ui() { return ui_; }
    PhoneNavigation& navigation() { return navigation_; }
    PhoneAppRegistry& registry() { return registry_; }
    PhoneServices& services() { return services_; }
    Settings& settings() { return settings_; }

private:
    PhoneUi& ui_;
    PhoneNavigation& navigation_;
    PhoneAppRegistry& registry_;
    PhoneServices& services_;
    Settings& settings_;
};
