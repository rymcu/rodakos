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

PhoneAppHostState PhoneNavigation::GetAppHostState() const {
    return system_.GetAppHostState();
}
