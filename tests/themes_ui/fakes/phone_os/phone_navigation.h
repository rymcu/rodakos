#pragma once

#include <string>
#include <string_view>
#include <vector>

class PhoneNavigation {
public:
    bool Launch(std::string_view app_id) {
        launches.emplace_back(app_id);
        return true;
    }
    bool ReturnHome() {
        ++home_count;
        return true;
    }
    // Without a recorded caller the production navigation falls back to ReturnHome().
    bool ReturnToCaller() { return ReturnHome(); }
    bool RefreshTheme() {
        ++refresh_count;
        return true;
    }

    std::vector<std::string> launches;
    int home_count = 0;
    int refresh_count = 0;
};
