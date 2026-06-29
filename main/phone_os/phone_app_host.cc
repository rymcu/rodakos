#include "phone_os/phone_app_host.h"

#include "phone_os/phone_app_context.h"

#include <esp_log.h>

namespace {
constexpr const char* TAG = "PhoneAppHost";
}

bool PhoneAppHost::Launch(const PhoneAppDescriptor& descriptor, PhoneAppContext& context) {
    ESP_LOGI(TAG, "Launching app: %s", descriptor.id.c_str());
    if (current_app_id_ == descriptor.id) {
        ESP_LOGI(TAG, "App already active: %s", descriptor.id.c_str());
        return true;
    }

    CloseCurrent(true);

    if (background_ && background_app_id_ == descriptor.id) {
        current_ = std::move(background_);
        current_app_id_ = background_app_id_;
        current_capabilities_ = background_capabilities_;
        background_app_id_.clear();
        background_capabilities_ = PhoneCapability::kNone;
        current_->OnShow();
        ESP_LOGI(TAG, "Restored background app: %s", current_app_id_.c_str());
        return true;
    }

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
    current_capabilities_ = descriptor.capabilities;
    current_->OnShow();
    ESP_LOGI(TAG, "App launched: %s", current_app_id_.c_str());
    return true;
}

void PhoneAppHost::CloseCurrent() {
    CloseCurrent(false);
}

void PhoneAppHost::CloseCurrent(bool allow_background) {
    if (!current_) {
        current_app_id_.clear();
        current_capabilities_ = PhoneCapability::kNone;
        return;
    }
    ESP_LOGI(TAG, "Closing app: %s", current_app_id_.c_str());
    current_->OnHide();

    if (allow_background && HasCapability(current_capabilities_, PhoneCapability::kBackgroundTick)) {
        if (background_) {
            ESP_LOGI(TAG, "Destroying previous background app: %s", background_app_id_.c_str());
            background_->OnDestroy();
        }
        background_ = std::move(current_);
        background_app_id_ = current_app_id_;
        background_capabilities_ = current_capabilities_;
        current_app_id_.clear();
        current_capabilities_ = PhoneCapability::kNone;
        ESP_LOGI(TAG, "Backgrounded app: %s", background_app_id_.c_str());
        return;
    }

    current_->OnDestroy();
    current_.reset();
    current_app_id_.clear();
    current_capabilities_ = PhoneCapability::kNone;
}
