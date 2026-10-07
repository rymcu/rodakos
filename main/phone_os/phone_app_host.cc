#include "phone_os/phone_app_host.h"

#include "phone_os/phone_app_context.h"
#include "phone_ui/phone_ui.h"

#include <esp_log.h>
#include <cstdlib>

namespace {
constexpr const char* TAG = "PhoneAppHost";
class TransitionGuard {
public:
    explicit TransitionGuard(bool& active) : active_(active) { active_ = true; }
    ~TransitionGuard() { active_ = false; }
private:
    bool& active_;
};
void DestroyApp(PhoneApp& app) noexcept {
    try {
        app.OnDestroy();
    } catch (...) {
        // Continuing would let unique_ptr destroy an owner of live LVGL timer
        // userdata. An incomplete teardown is not a recoverable launch failure.
        ESP_LOGE(TAG, "App teardown threw; cannot safely resume navigation");
        std::abort();
    }
}
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
    if (transition_in_progress_) {
        ESP_LOGW(TAG, "Ignoring re-entrant launch: %s", descriptor.id.c_str());
        return false;
    }

    TransitionGuard transition(transition_in_progress_);
    ESP_LOGI(TAG, "Launching app: %s", descriptor.id.c_str());
    const bool launched = [&]() {
        if (current_ != nullptr && current_app_id_ == descriptor.id &&
            RefreshThemeIfNeeded(*current_, context, current_theme_revision_)) {
            context.ui().ResetInputState();
            ESP_LOGI(TAG, "App already active: %s", descriptor.id.c_str());
            return true;
        }

        return CreateAndReplace(descriptor, context);
    }();
    return launched;
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
    if (transition_in_progress_) {
        ESP_LOGW(TAG, "Ignoring re-entrant recreate: %s", descriptor.id.c_str());
        return false;
    }

    TransitionGuard transition(transition_in_progress_);
    ESP_LOGI(TAG, "Recreating current app: %s", descriptor.id.c_str());
    const bool recreated = CreateAndReplace(descriptor, context);
    return recreated;
}

bool PhoneAppHost::HandleHomeRequest() {
    if (transition_in_progress_ || current_ == nullptr) {
        return false;
    }
    TransitionGuard transition(transition_in_progress_);
    const bool handled = current_->OnHomeRequested();
    return handled;
}

bool PhoneAppHost::HandleBackRequest() {
    if (transition_in_progress_ || current_ == nullptr) return false;
    TransitionGuard transition(transition_in_progress_);
    const bool handled = current_->OnBackRequested();
    return handled;
}

bool PhoneAppHost::CreateAndReplace(const PhoneAppDescriptor& descriptor,
                                    PhoneAppContext& context) {
    if (!descriptor.create) {
        ESP_LOGE(TAG, "App %s has no factory", descriptor.id.c_str());
        return false;
    }

    std::unique_ptr<PhoneApp> next;
    std::string next_app_id;
    try {
        next_app_id = descriptor.id;
        next = descriptor.create();
    } catch (...) {
        ESP_LOGE(TAG, "App factory failed: %s", descriptor.id.c_str());
        return false;
    }
    if (!next) {
        ESP_LOGE(TAG, "App %s factory returned null", descriptor.id.c_str());
        return false;
    }

    bool created = false;
    try {
        created = next->OnCreate(context);
    } catch (...) {
        DestroyApp(*next);
        ESP_LOGE(TAG, "App creation threw: %s", descriptor.id.c_str());
        return false;
    }
    if (!created) {
        DestroyApp(*next);
        ESP_LOGE(TAG, "App %s OnCreate failed; keeping current app", descriptor.id.c_str());
        return false;
    }

    context.ui().ResetInputState();
    DestroyCurrent();
    current_ = std::move(next);
    current_app_id_ = std::move(next_app_id);
    current_capabilities_ = descriptor.capabilities;
    current_theme_revision_ = context.ui().theme_revision();
    current_->OnResume();
    context.ui().ResetInputState();
    ESP_LOGI(TAG, "App launched: %s", current_app_id_.c_str());
    return true;
}

PhoneAppHostState PhoneAppHost::GetState() const {
    PhoneAppHostState state;
    state.current_app_id = current_app_id_;
    state.current_capabilities = current_capabilities_;
    state.has_current = current_ != nullptr;
    state.transition_in_progress = transition_in_progress_;
    return state;
}

void PhoneAppHost::CloseCurrent() {
    if (transition_in_progress_) {
        return;
    }
    TransitionGuard transition(transition_in_progress_);
    DestroyCurrent();
}

void PhoneAppHost::DestroyCurrent() {
    if (!current_) {
        current_app_id_.clear();
        current_capabilities_ = PhoneCapability::kNone;
        current_theme_revision_ = 0;
        return;
    }
    ESP_LOGI(TAG, "Closing app: %s", current_app_id_.c_str());
    try {
        current_->OnPause();
    } catch (...) {
        ESP_LOGE(TAG, "App pause threw during teardown");
        std::abort();
    }
    DestroyApp(*current_);
    current_.reset();
    current_app_id_.clear();
    current_capabilities_ = PhoneCapability::kNone;
    current_theme_revision_ = 0;
}
