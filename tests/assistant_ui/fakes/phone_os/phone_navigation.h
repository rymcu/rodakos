#pragma once
#include <string>
#include <string_view>
#include <vector>
class PhoneNavigation {
public:
    bool Launch(std::string_view id) { launches.emplace_back(id); return true; }
    void ReturnHome() { ++home_count; }
    void RefreshTheme() {}
    std::vector<std::string> launches;
    int home_count = 0;
};
