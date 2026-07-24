#pragma once

#include "phone_os/phone_app.h"

#include <string>
#include <vector>

class PhoneAppRegistry {
public:
    void Register(PhoneAppDescriptor descriptor);
    bool Finalize();
    const PhoneAppDescriptor* FindById(std::string_view id) const;
    const PhoneAppDescriptor* FindHome() const { return home_app_; }
    const PhoneAppDescriptor* ResolveAlias(std::string_view text) const;
    std::vector<const PhoneAppDescriptor*> ListHomeApps() const;
    const std::vector<PhoneAppDescriptor>& apps() const { return apps_; }
    bool finalized() const { return finalized_; }
    bool valid() const { return valid_; }

private:
    std::vector<PhoneAppDescriptor> apps_;
    const PhoneAppDescriptor* home_app_ = nullptr;
    bool finalized_ = false;
    bool valid_ = false;
};
