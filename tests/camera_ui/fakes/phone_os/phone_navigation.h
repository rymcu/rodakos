#pragma once
#include "phone_os/deferred_navigation.h"
class PhoneNavigation {
public:
    PhoneNavigation() {
        deferred.Initialize([](void* p, std::string_view, bool) {
            static_cast<PhoneNavigation*>(p)->ReturnHome();
            return true;
        }, this);
    }
    bool RequestHome() { return deferred.Enqueue({}, true); }
    void ReturnHome() { ++home_count; }
    int home_count = 0;
    DeferredNavigation deferred;
};
