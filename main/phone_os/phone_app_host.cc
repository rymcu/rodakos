#include "phone_os/phone_app_host.h"

#include "phone_os/phone_app_context.h"

#include <esp_log.h>

namespace {
constexpr const char* TAG = "PhoneAppHost";
}

bool PhoneAppHost::Launch(const PhoneAppDescriptor& descriptor, PhoneAppContext& context) {
    CloseCurrent();
    if (!descriptor.create) {
        ESP_LOGE(TAG, "App %s has no factory", descriptor.id.c_str());
        return false;
    }

    current_ = descriptor.create();
    if (!current_) {
        ESP_LOGE(TAG, "App %s factory returned null", descriptor.id.c_str());
        return false;
    }

    if (!current_->OnCreate(context)) {
        current_.reset();
        current_app_id_.clear();
        ESP_LOGE(TAG, "App %s OnCreate failed", descriptor.id.c_str());
        return false;
    }

    current_app_id_ = descriptor.id;
    current_->OnShow();
    return true;
}

void PhoneAppHost::CloseCurrent() {
    if (!current_) {
        current_app_id_.clear();
        return;
    }
    current_->OnHide();
    current_->OnDestroy();
    current_.reset();
    current_app_id_.clear();
}
