#pragma once

#include "phone_os/phone_app.h"

#include <memory>

class PhoneAppContext;
class PhoneAppRegistry;

class PhoneAppHost {
public:
    bool Launch(const PhoneAppDescriptor& descriptor, PhoneAppContext& context);
    void CloseCurrent();
    PhoneApp* current_app() { return current_.get(); }
    const std::string& current_app_id() const { return current_app_id_; }

private:
    std::unique_ptr<PhoneApp> current_;
    std::string current_app_id_;
};
