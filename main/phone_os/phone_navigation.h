#pragma once

#include "phone_os/phone_app_host.h"

#include <string_view>

class PhoneSystem;

class PhoneNavigation {
public:
    explicit PhoneNavigation(PhoneSystem& system) : system_(system) {}

    bool Launch(std::string_view app_id);
    bool RefreshTheme();
    bool ReturnHome();
    PhoneAppHostState GetAppHostState() const;

private:
    PhoneSystem& system_;
};
