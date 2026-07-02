#include "phone_os/phone_system.h"

#include "apps/built_in_apps.h"
#include "phone_ui/phone_ui.h"

#include <esp_log.h>

namespace {
constexpr const char* TAG = "PhoneSystem";
constexpr const char* kHomeAppId = "home";
}  // namespace

PhoneSystem::PhoneSystem(PhoneUi& ui, PhoneServices& services)
    : ui_(ui),
      navigation_(*this),
      services_(services),
      settings_("rodakos", true),
      context_(ui_, navigation_, registry_, services_, settings_) {}

void PhoneSystem::RegisterBuiltInApps() {
    RegisterRodakBuiltInApps(registry_);
}

bool PhoneSystem::Start() {
    RegisterBuiltInApps();
    return ReturnHome();
}

bool PhoneSystem::LaunchApp(std::string_view app_id) {
    ESP_LOGI(TAG, "Launch requested: %.*s", static_cast<int>(app_id.size()), app_id.data());
    const auto* descriptor = registry_.FindById(app_id);
    if (descriptor == nullptr) {
        ESP_LOGE(TAG, "Unknown app: %.*s", static_cast<int>(app_id.size()), app_id.data());
        return false;
    }
    return host_.Launch(*descriptor, context_);
}

bool PhoneSystem::RefreshTheme() {
    ESP_LOGI(TAG, "Theme refresh requested");
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
    return LaunchApp(kHomeAppId);
}
