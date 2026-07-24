#pragma once

#include <string>
#include <string_view>
#include <vector>

class PhoneNavigation {
public:
    bool Launch(std::string_view app_id) {
        launches.emplace_back(app_id);
        return launch_result;
    }

    bool launch_result = true;
    std::vector<std::string> launches;
};
