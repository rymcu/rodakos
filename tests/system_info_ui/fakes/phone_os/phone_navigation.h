#pragma once
#include "phone_os/phone_app.h"
#include <string>
#include <string_view>
struct PhoneAppHostState {
    std::string current_app_id;
    PhoneCapability current_capabilities = PhoneCapability::kNone;
    bool has_current = false;
    bool transition_in_progress = false;
};
class PhoneNavigation {
public:
    bool ReturnHome() { ++home_count; return true; }
    bool ReturnToCaller() { ++caller_count; return true; }
    PhoneAppHostState GetAppHostState() const { return host; }
    PhoneAppHostState host;
    int home_count = 0;
    int caller_count = 0;
};
