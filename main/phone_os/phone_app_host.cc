#include "phone_os/phone_app_host.h"

#include "phone_os/phone_app_context.h"
#include "phone_ui/phone_ui.h"

#include <esp_log.h>

namespace {
constexpr const char* TAG = "PhoneAppHost";
}

bool PhoneAppHost::RefreshThemeIfNeeded(PhoneApp& app,
                                        PhoneAppContext& context,
                                        uint32_t& app_theme_revision) {
    const uint32_t theme_revision = context.ui().theme_revision();
    if (app_theme_revision == theme_revision) {
        return true;
    }
    if (!app.OnThemeChanged(context)) {
        return false;
    }
    app_theme_revision = theme_revision;
    return true;
}

bool PhoneAppHost::Launch(const PhoneAppDescriptor& descriptor, PhoneAppContext& context) {
    ESP_LOGI(TAG, "Launching app: %s", descriptor.id.c_str());
    const uint32_t theme_revision = context.ui().theme_revision();
    if (current_app_id_ == descriptor.id) {
        if (RefreshThemeIfNeeded(*current_, context, current_theme_revision_)) {
            ESP_LOGI(TAG, "App already active: %s", descriptor.id.c_str());
            return true;
        }
        ESP_LOGI(TAG, "Recreating active app for theme update: %s", descriptor.id.c_str());
        context.ui().ResetInputState();
        CloseCurrent(false);
    }

    context.ui().ResetInputState();
    CloseCurrent(true);

    if (background_ && background_app_id_ == descriptor.id) {
        if (!RefreshThemeIfNeeded(*background_, context, background_theme_revision_)) {
            ESP_LOGI(TAG, "Recreating stale background app for theme update: %s", background_app_id_.c_str());
            background_->OnDestroy();
            background_.reset();
            background_app_id_.clear();
            background_capabilities_ = PhoneCapability::kNone;
            background_theme_revision_ = 0;
        } else {
            current_ = std::move(background_);
            current_app_id_ = background_app_id_;
            current_capabilities_ = background_capabilities_;
            current_theme_revision_ = background_theme_revision_;
            background_app_id_.clear();
            background_capabilities_ = PhoneCapability::kNone;
            background_theme_revision_ = 0;
            current_->OnShow();
            ESP_LOGI(TAG, "Restored background app: %s", current_app_id_.c_str());
            return true;
        }
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
    current_theme_revision_ = theme_revision;
    current_->OnShow();
    ESP_LOGI(TAG, "App launched: %s", current_app_id_.c_str());
    return true;
}

bool PhoneAppHost::RefreshCurrentTheme(PhoneAppContext& context) {
    if (!current_) {
        return true;
    }
    if (RefreshThemeIfNeeded(*current_, context, current_theme_revision_)) {
        ESP_LOGI(TAG, "Refreshed current app theme: %s", current_app_id_.c_str());
        return true;
    }
    ESP_LOGI(TAG, "Current app does not support live theme refresh: %s", current_app_id_.c_str());
    return false;
}

bool PhoneAppHost::RecreateCurrent(const PhoneAppDescriptor& descriptor, PhoneAppContext& context) {
    ESP_LOGI(TAG, "Recreating current app: %s", descriptor.id.c_str());
    CloseCurrent(false);
    return Launch(descriptor, context);
}

void PhoneAppHost::CloseCurrent() {
    CloseCurrent(false);
}

void PhoneAppHost::CloseCurrent(bool allow_background) {
    if (!current_) {
        current_app_id_.clear();
        current_capabilities_ = PhoneCapability::kNone;
        current_theme_revision_ = 0;
        return;
    }
    ESP_LOGI(TAG, "Closing app: %s", current_app_id_.c_str());
    current_->OnHide();

    if (allow_background && HasCapability(current_capabilities_, PhoneCapability::kBackgroundTick)) {
        if (background_) {
            ESP_LOGI(TAG, "Destroying previous background app: %s", background_app_id_.c_str());
            background_->OnDestroy();
            background_theme_revision_ = 0;
        }
        background_ = std::move(current_);
        background_app_id_ = current_app_id_;
        background_capabilities_ = current_capabilities_;
        background_theme_revision_ = current_theme_revision_;
        current_app_id_.clear();
        current_capabilities_ = PhoneCapability::kNone;
        current_theme_revision_ = 0;
        ESP_LOGI(TAG, "Backgrounded app: %s", background_app_id_.c_str());
        return;
    }

    current_->OnDestroy();
    current_.reset();
    current_app_id_.clear();
    current_capabilities_ = PhoneCapability::kNone;
    current_theme_revision_ = 0;
}
