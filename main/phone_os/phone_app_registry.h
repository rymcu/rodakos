#pragma once

#include "phone_os/phone_app.h"

#include <vector>

class PhoneAppRegistry {
public:
    void Register(PhoneAppDescriptor descriptor);
    const PhoneAppDescriptor* FindById(std::string_view id) const;
    const PhoneAppDescriptor* ResolveAlias(std::string_view text) const;
    std::vector<const PhoneAppDescriptor*> ListHomeApps() const;
    const std::vector<PhoneAppDescriptor>& apps() const { return apps_; }

private:
    std::vector<PhoneAppDescriptor> apps_;
};
