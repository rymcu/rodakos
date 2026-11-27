#pragma once
#include <string>
#include <string_view>
#include <vector>
class PhoneNavigation {
public:
    bool ReturnHome() { ++home_count; return true; }
    bool LaunchFrom(std::string_view id, std::string_view caller, std::string_view page = {}) {
        launches.emplace_back(std::string(id) + "<" + std::string(caller) + ":" + std::string(page));
        return true;
    }
    int home_count = 0;
    std::vector<std::string> launches;
};
