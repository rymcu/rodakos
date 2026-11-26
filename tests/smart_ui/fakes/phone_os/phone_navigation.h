#pragma once
class PhoneNavigation {
public:
    bool ReturnHome() { ++home_count; return true; }
    int home_count = 0;
};
