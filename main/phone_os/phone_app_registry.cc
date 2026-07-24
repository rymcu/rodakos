#include "phone_os/phone_app_registry.h"

#include <algorithm>
#include <cctype>
#include <esp_log.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {
constexpr const char* TAG = "PhoneAppRegistry";

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
    if (finalized_) {
        ESP_LOGE(TAG, "Cannot register app '%s' after registry finalization", descriptor.id.c_str());
        valid_ = false;
        return;
    }
    apps_.push_back(std::move(descriptor));
}

bool PhoneAppRegistry::Finalize() {
    if (finalized_) {
        return valid_;
    }

    finalized_ = true;
    valid_ = true;
    home_app_ = nullptr;

    std::unordered_map<std::string, const PhoneAppDescriptor*> identities;
    for (const auto& app : apps_) {
        const std::string normalized_id = Normalize(app.id);
        if (normalized_id.empty() || app.title.empty() || !app.create) {
            ESP_LOGE(TAG, "Invalid app descriptor: id='%s' title='%s' factory=%d",
                     app.id.c_str(), app.title.c_str(), app.create ? 1 : 0);
            valid_ = false;
            continue;
        }

        if (app.role == PhoneAppRole::kHome) {
            if (home_app_ != nullptr) {
                ESP_LOGE(TAG, "Multiple Home apps: '%s' and '%s'",
                         home_app_->id.c_str(), app.id.c_str());
                valid_ = false;
            } else {
                home_app_ = &app;
            }
            if (app.show_on_home) {
                ESP_LOGE(TAG, "Home app '%s' cannot be visible in its own launcher", app.id.c_str());
                valid_ = false;
            }
        }

        std::unordered_set<std::string> app_identities;
        auto register_identity = [&](std::string_view value) {
            const std::string normalized = Normalize(value);
            if (normalized.empty() || !app_identities.insert(normalized).second) {
                return;
            }
            const auto [it, inserted] = identities.emplace(normalized, &app);
            if (!inserted && it->second != &app) {
                ESP_LOGE(TAG, "App identity '%s' conflicts between '%s' and '%s'",
                         normalized.c_str(), it->second->id.c_str(), app.id.c_str());
                valid_ = false;
            }
        };

        register_identity(app.id);
        register_identity(app.title);
        for (const auto& alias : app.aliases) {
            register_identity(alias);
        }
    }

    if (home_app_ == nullptr) {
        ESP_LOGE(TAG, "No Home app registered");
        valid_ = false;
    }

    ESP_LOGI(TAG, "Registry finalized with %u apps", static_cast<unsigned>(apps_.size()));
    return valid_;
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
