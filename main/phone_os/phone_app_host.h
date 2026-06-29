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
    void CloseCurrent(bool allow_background);

    std::unique_ptr<PhoneApp> current_;
    std::unique_ptr<PhoneApp> background_;
    std::string current_app_id_;
    std::string background_app_id_;
    PhoneCapability current_capabilities_ = PhoneCapability::kNone;
    PhoneCapability background_capabilities_ = PhoneCapability::kNone;
};
