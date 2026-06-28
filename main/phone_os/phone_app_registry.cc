#include "phone_os/phone_app_registry.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {
std::string Normalize(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        const auto raw = static_cast<unsigned char>(ch);
        if (std::isalnum(raw)) {
            out.push_back(static_cast<char>(std::tolower(raw)));
        } else if (raw >= 0x80) {
            out.push_back(ch);
        }
    }
    return out;
}
}  // namespace

void PhoneAppRegistry::Register(PhoneAppDescriptor descriptor) {
    apps_.push_back(std::move(descriptor));
}

const PhoneAppDescriptor* PhoneAppRegistry::FindById(std::string_view id) const {
    const std::string needle = Normalize(id);
    for (const auto& app : apps_) {
        if (Normalize(app.id) == needle) {
            return &app;
        }
    }
    return nullptr;
}

const PhoneAppDescriptor* PhoneAppRegistry::ResolveAlias(std::string_view text) const {
    const std::string needle = Normalize(text);
    for (const auto& app : apps_) {
        if (Normalize(app.id) == needle || Normalize(app.title) == needle) {
            return &app;
        }
        for (const auto& alias : app.aliases) {
            if (Normalize(alias) == needle) {
                return &app;
            }
        }
    }
    return nullptr;
}

std::vector<const PhoneAppDescriptor*> PhoneAppRegistry::ListHomeApps() const {
    std::vector<const PhoneAppDescriptor*> result;
    for (const auto& app : apps_) {
        if (app.show_on_home) {
            result.push_back(&app);
        }
    }
    return result;
}
