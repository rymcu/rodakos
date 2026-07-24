#pragma once

#include "phone_os/phone_app.h"

#include <cstdint>
#include <memory>
#include <string>

class PhoneAppContext;
class PhoneAppRegistry;

struct PhoneAppHostState {
    std::string current_app_id;
    PhoneCapability current_capabilities = PhoneCapability::kNone;
    bool has_current = false;
    bool transition_in_progress = false;
};

class PhoneAppHost {
public:
    bool Launch(const PhoneAppDescriptor& descriptor, PhoneAppContext& context);
    bool RefreshCurrentTheme(PhoneAppContext& context);
    bool RecreateCurrent(const PhoneAppDescriptor& descriptor, PhoneAppContext& context);
    bool HandleHomeRequest();
    void CloseCurrent();
    PhoneApp* current_app() { return current_.get(); }
    const std::string& current_app_id() const { return current_app_id_; }
    PhoneAppHostState GetState() const;

private:
    bool RefreshThemeIfNeeded(PhoneApp& app, PhoneAppContext& context, uint32_t& app_theme_revision);
    bool CreateAndReplace(const PhoneAppDescriptor& descriptor, PhoneAppContext& context);
    void DestroyCurrent();

    std::unique_ptr<PhoneApp> current_;
    std::string current_app_id_;
    PhoneCapability current_capabilities_ = PhoneCapability::kNone;
    uint32_t current_theme_revision_ = 0;
    bool transition_in_progress_ = false;
};
