#include "phone_os/phone_system.h"

#include "apps/built_in_apps.h"
#include "phone_ui/phone_ui.h"

#include <esp_log.h>

namespace {
constexpr const char* TAG = "PhoneSystem";
}  // namespace

PhoneSystem::PhoneSystem(PhoneUi& ui, PhoneServices& services)
    : ui_(ui),
      navigation_(*this),
      services_(services),
      shell_(*this, ui_, services_),
      settings_("rodakos", true),
      context_(ui_, navigation_, registry_, services_, settings_) {}

void PhoneSystem::RegisterBuiltInApps() {
    RegisterRodakBuiltInApps(registry_);
}

bool PhoneSystem::Start() {
    ESP_LOGI(TAG, "Starting Phone OS");
    RegisterBuiltInApps();
    if (!registry_.Finalize()) {
        ESP_LOGE(TAG, "App registry validation failed");
        return false;
    }
    if (!shell_.Initialize()) {
        ESP_LOGE(TAG, "System shell initialization failed");
        return false;
    }

    const auto* home = registry_.FindHome();
    if (home == nullptr) {
        ESP_LOGE(TAG, "Home app is unavailable after registry finalization");
        return false;
    }
    return host_.Launch(*home, context_);
}

bool PhoneSystem::LaunchApp(std::string_view app_id) {
    ESP_LOGI(TAG, "Launch requested: %.*s", static_cast<int>(app_id.size()), app_id.data());
    if (!shell_.PrepareNavigation()) {
        return false;
    }
    const auto* descriptor = registry_.FindById(app_id);
    if (descriptor == nullptr) {
        ESP_LOGE(TAG, "Unknown app: %.*s", static_cast<int>(app_id.size()), app_id.data());
        return false;
    }
    return host_.Launch(*descriptor, context_);
}

bool PhoneSystem::RefreshTheme() {
    ESP_LOGI(TAG, "Theme refresh requested");
    if (!shell_.PrepareNavigation()) {
        return false;
    }
    ui_.ResetInputState();
    if (host_.RefreshCurrentTheme(context_)) {
        return true;
    }
    const std::string app_id(host_.current_app_id());
    if (app_id.empty()) {
        return false;
    }
    const auto* descriptor = registry_.FindById(app_id);
    if (descriptor == nullptr) {
        ESP_LOGE(TAG, "Cannot recreate unknown app for theme refresh: %s", app_id.c_str());
        return false;
    }
    return host_.RecreateCurrent(*descriptor, context_);
}

bool PhoneSystem::ReturnHome() {
    ESP_LOGI(TAG, "Return home requested");
    const auto* home = registry_.FindHome();
    if (home == nullptr) {
        ESP_LOGE(TAG, "No Home app registered");
        return false;
    }
    if (host_.current_app_id() == home->id) {
        if (!shell_.PrepareNavigation()) {
            return false;
        }
        if (host_.HandleHomeRequest()) {
            return true;
        }
    }
    return LaunchApp(home->id);
}

bool PhoneSystem::Lock() {
    return shell_.Lock();
}

bool PhoneSystem::ToggleControlCenter() {
    return shell_.ToggleControlCenter();
}
