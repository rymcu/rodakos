#pragma once

#include <string_view>

class PhoneSystem;

class PhoneNavigation {
public:
    explicit PhoneNavigation(PhoneSystem& system) : system_(system) {}

    bool Launch(std::string_view app_id);
    bool ReturnHome();

private:
    PhoneSystem& system_;
};
