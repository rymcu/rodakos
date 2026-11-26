#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>
class PhoneNavigation {
public:
    bool Launch(std::string_view id) { launches.emplace_back(id); return true; }
    bool LaunchFrom(std::string_view id, std::string_view caller, std::string_view page = {}) {
        callers.emplace_back(caller);
        pages.emplace_back(page);
        requested_page.assign(page);
        return Launch(id);
    }
    std::string TakeRequestedPage() { return std::exchange(requested_page, std::string()); }
    bool ReturnToCaller() { ++caller_count; return true; }
    void ReturnHome() { ++home_count; }
    void RefreshTheme() {}
    std::vector<std::string> launches;
    std::vector<std::string> callers;
    std::vector<std::string> pages;
    std::string requested_page;
    int home_count = 0;
    int caller_count = 0;
};
