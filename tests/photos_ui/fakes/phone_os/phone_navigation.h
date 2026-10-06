#pragma once
#include <string_view>
class PhoneNavigation {
public:
    void ReturnHome() { ++home_count; }
    bool Launch(std::string_view) { return true; }
    int home_count = 0;
};
