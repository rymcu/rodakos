#include "phone_os/button_binding_service.h"

#include "phone_os/phone_navigation.h"
#include "phone_ui/phone_ui.h"
#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>

#include <esp_log.h>
#include <esp_lvgl_port.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "ButtonBinding";
constexpr const char* kSettingsNamespace = "btnbind";
constexpr const char* kLaunchPrefix = "launch:";
constexpr TickType_t kSingleClickDelayTicks = pdMS_TO_TICKS(280);
constexpr UBaseType_t kEventQueueDepth = 8;
constexpr uint32_t kWorkerStackWords = 6144;

bool IsSupportedEvent(ButtonEvent event) {
    switch (event) {
        case ButtonEvent::kSingleClick:
        case ButtonEvent::kDoubleClick:
        case ButtonEvent::kLongPressStart:
            return true;
        default:
            return false;
    }
}

ButtonEvent ToButtonEvent(BoardButtonEvent event) {
    switch (event) {
        case BoardButtonEvent::kSingleClick:
            return ButtonEvent::kSingleClick;
        case BoardButtonEvent::kDoubleClick:
            return ButtonEvent::kDoubleClick;
        case BoardButtonEvent::kLongPressStart:
            return ButtonEvent::kLongPressStart;
        default:
            return ButtonEvent::kSingleClick;
    }
}

std::string NormalizeId(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        const auto raw = static_cast<unsigned char>(ch);
        if (std::isalnum(raw)) {
            out.push_back(static_cast<char>(std::tolower(raw)));
        } else if (ch == '_' || ch == '-') {
            out.push_back('_');
        }
    }
    return out;
}

uint32_t Fnv1a(std::string_view value) {
    uint32_t hash = 2166136261u;
    for (char ch : value) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 16777619u;
    }
    return hash;
}

char EventCode(ButtonEvent event) {
    switch (event) {
        case ButtonEvent::kSingleClick:
            return 's';
        case ButtonEvent::kDoubleClick:
            return 'd';
        case ButtonEvent::kLongPressStart:
            return 'l';
        default:
            return 'x';
    }
}

std::string TitleCase(std::string_view value) {
    std::string title;
    title.reserve(value.size());
    bool upper_next = true;
    for (char ch : value) {
        if (ch == '_' || ch == '-' || ch == ':') {
            title.push_back(' ');
            upper_next = true;
            continue;
        }
        if (upper_next) {
            title.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            upper_next = false;
        } else {
            title.push_back(ch);
        }
    }
    return title.empty() ? "Button" : title;
}

}  // namespace

bool ButtonBindingService::Init(PhoneNavigation& navigation, PhoneUi& ui) {
    if (initialized_) {
        return IsAvailable();
    }

    navigation_ = &navigation;
    ui_ = &ui;
    buttons_.clear();
    registered_.clear();

    DiscoverButtons();
    click_generations_.assign(registered_.size(), 0);
    click_timers_.assign(registered_.size(), nullptr);
    if (!registered_.empty() && !StartWorker()) {
        ESP_LOGW(TAG, "Button worker unavailable; hardware bindings disabled");
        ResetDiscoveredState();
        return false;
    }
    RegisterCallbacks();

    initialized_ = true;
    ESP_LOGI(TAG, "Discovered %u board button(s)", static_cast<unsigned>(buttons_.size()));
    return IsAvailable();
}

std::vector<ButtonBinding> ButtonBindingService::ListBindings() const {
    std::vector<ButtonBinding> result;
    result.reserve(buttons_.size() * 3);
    for (const auto& button : buttons_) {
        for (ButtonEvent event : kButtonEvents) {
            result.push_back(GetBinding(button.id, event));
        }
    }
    return result;
}

const ButtonState* ButtonBindingService::GetButton(std::string_view button_id) const {
    return FindButton(button_id);
}

ButtonBinding ButtonBindingService::GetBinding(std::string_view button_id, ButtonEvent event) const {
    const std::string normalized_id = NormalizeId(button_id);
    ButtonBinding binding;
    binding.button_id = normalized_id;
    binding.title = MakeTitle(normalized_id) + " " + EventLabel(event);
    binding.event = event;
    binding.action = DefaultAction(normalized_id, event);

    Settings settings(kSettingsNamespace, false);
    const std::string encoded = settings.GetString(StorageKey(normalized_id, event), "");
    if (!encoded.empty()) {
        binding.action = DecodeAction(encoded);
        binding.custom = true;
    }
    return binding;
}

bool ButtonBindingService::SetBinding(std::string_view button_id,
                                      ButtonEvent event,
                                      const ButtonAction& action) {
    const std::string normalized_id = NormalizeId(button_id);
    if (!IsSupportedEvent(event) || FindButton(normalized_id) == nullptr) {
        return false;
    }

    Settings settings(kSettingsNamespace, true);
    if (!settings.SetString(StorageKey(normalized_id, event), EncodeAction(action)) ||
        !settings.Commit()) {
        ESP_LOGE(TAG, "Failed to persist binding for %.*s %s",
                 static_cast<int>(normalized_id.size()), normalized_id.data(), EventName(event));
        return false;
    }
    ESP_LOGI(TAG, "Binding saved: %.*s %s -> %s",
             static_cast<int>(normalized_id.size()), normalized_id.data(),
             EventName(event), EncodeAction(action).c_str());
    return true;
}

bool ButtonBindingService::ResetBinding(std::string_view button_id, ButtonEvent event) {
    const std::string normalized_id = NormalizeId(button_id);
    if (!IsSupportedEvent(event) || FindButton(normalized_id) == nullptr) {
        return false;
    }

    Settings settings(kSettingsNamespace, true);
    if (!settings.SetString(StorageKey(normalized_id, event), "") || !settings.Commit()) {
        ESP_LOGE(TAG, "Failed to reset binding for %.*s %s",
                 static_cast<int>(normalized_id.size()), normalized_id.data(), EventName(event));
        return false;
    }
    ESP_LOGI(TAG, "Binding reset: %.*s %s",
             static_cast<int>(normalized_id.size()), normalized_id.data(), EventName(event));
    return true;
}

const char* ButtonBindingService::EventName(ButtonEvent event) {
    switch (event) {
        case ButtonEvent::kSingleClick:
            return "single";
        case ButtonEvent::kDoubleClick:
            return "double";
        case ButtonEvent::kLongPressStart:
            return "long";
        default:
            return "unknown";
    }
}

const char* ButtonBindingService::EventLabel(ButtonEvent event) {
    switch (event) {
        case ButtonEvent::kSingleClick:
            return "Single";
        case ButtonEvent::kDoubleClick:
            return "Double";
        case ButtonEvent::kLongPressStart:
            return "Long";
        default:
            return "Event";
    }
}

std::string ButtonBindingService::EncodeAction(const ButtonAction& action) {
    switch (action.type) {
        case ButtonActionType::kHome:
            return "home";
        case ButtonActionType::kLock:
            return "lock";
        case ButtonActionType::kToggleControlCenter:
            return "control_center";
        case ButtonActionType::kLaunchApp:
            return std::string(kLaunchPrefix) + action.app_id;
        case ButtonActionType::kNone:
        default:
            return "none";
    }
}

ButtonAction ButtonBindingService::DecodeAction(std::string_view encoded) {
    if (encoded == "home") {
        return ButtonAction{.type = ButtonActionType::kHome, .app_id = ""};
    }
    if (encoded == "lock") {
        return ButtonAction{.type = ButtonActionType::kLock, .app_id = ""};
    }
    if (encoded == "control_center") {
        return ButtonAction{.type = ButtonActionType::kToggleControlCenter, .app_id = ""};
    }
    if (encoded.rfind(kLaunchPrefix, 0) == 0) {
        return ButtonAction{
            .type = ButtonActionType::kLaunchApp,
            .app_id = std::string(encoded.substr(std::strlen(kLaunchPrefix))),
        };
    }
    return ButtonAction{.type = ButtonActionType::kNone, .app_id = ""};
}

std::string ButtonBindingService::ActionLabel(const ButtonAction& action) {
    switch (action.type) {
        case ButtonActionType::kHome:
            return "Home";
        case ButtonActionType::kLock:
            return "Lock";
        case ButtonActionType::kToggleControlCenter:
            return "Control Center";
        case ButtonActionType::kLaunchApp:
            return "Open " + TitleCase(action.app_id);
        case ButtonActionType::kNone:
        default:
            return "None";
    }
}

void ButtonBindingService::DiscoverButtons() {
    for (const auto& device : DiscoverBoardButtons()) {
        if (device.native_handle == nullptr) {
            continue;
        }

        const std::string normalized_id = NormalizeId(device.id);
        const std::string title = MakeTitle(normalized_id);
        BoardButtonDevice board_button = device;
        board_button.id = normalized_id;
        board_buttons_.push_back(std::move(board_button));

        buttons_.push_back(ButtonState{
            .id = normalized_id,
            .title = title,
            .device_name = device.device_name,
            .physical_index = device.physical_index,
            .available = true,
        });
        registered_.push_back(RegisteredButton{
            .button_id = normalized_id,
            .title = title,
            .device_name = device.device_name,
            .physical_index = device.physical_index,
        });
    }
}

void ButtonBindingService::RegisterCallbacks() {
    if (!RegisterBoardButtonCallbacks(board_buttons_, ButtonEventCallback, this)) {
        ESP_LOGW(TAG, "No board button callbacks were registered");
    }
}

void ButtonBindingService::ResetDiscoveredState() {
    buttons_.clear();
    registered_.clear();
    board_buttons_.clear();
    click_generations_.clear();
    click_timers_.clear();
    initialized_ = false;
}

bool ButtonBindingService::StartWorker() {
    if (worker_task_ != nullptr && event_queue_ != nullptr) {
        return true;
    }

    event_queue_ = xQueueCreate(kEventQueueDepth, sizeof(QueuedButtonEvent));
    if (event_queue_ == nullptr) {
        ESP_LOGW(TAG, "Failed to create button event queue");
        return false;
    }

    const BaseType_t ret = xTaskCreate(
        WorkerTask, "btn_bind", kWorkerStackWords, this, 4, &worker_task_);
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to start button worker task");
        vQueueDelete(event_queue_);
        event_queue_ = nullptr;
        return false;
    }
    return true;
}

void ButtonBindingService::HandleButtonEvent(std::string_view button_id, ButtonEvent event) {
    if (button_id.empty()) {
        return;
    }

    const auto* button = FindRegistered(button_id);
    if (button == nullptr) {
        return;
    }

    if (!IsSupportedEvent(event)) {
        return;
    }

    ESP_LOGI(TAG, "Button '%s' event: %s", button->button_id.c_str(), EventName(event));
    if (event == ButtonEvent::kSingleClick) {
        ScheduleSingleClick(*button);
        return;
    }

    CancelSingleClick(*button);
    QueueButtonEvent(button->button_id, event);
}

void ButtonBindingService::ExecuteBinding(const RegisteredButton& button, ButtonEvent event) {
    const ButtonBinding binding = GetBinding(button.button_id, event);
    if (binding.action.type == ButtonActionType::kNone) {
        ESP_LOGI(TAG, "Button '%s' %s has no action",
                 button.button_id.c_str(), EventName(event));
        return;
    }

    const std::string toast = button.title + ": " + ActionLabel(binding.action);
    ESP_LOGI(TAG, "Button '%s' %s -> %s",
             button.button_id.c_str(), EventName(event), EncodeAction(binding.action).c_str());
    ExecuteAction(binding.action, toast);
}

void ButtonBindingService::ScheduleSingleClick(const RegisteredButton& button) {
    const auto it = std::find_if(registered_.begin(), registered_.end(),
                                 [&button](const RegisteredButton& candidate) {
                                     return candidate.button_id == button.button_id;
                                 });
    if (it == registered_.end()) {
        return;
    }

    const size_t index = static_cast<size_t>(std::distance(registered_.begin(), it));
    if (index >= click_generations_.size() || index >= click_timers_.size()) {
        return;
    }

    const uint32_t generation = ++click_generations_[index];
    if (click_timers_[index] == nullptr) {
        auto* deferred = new DeferredClick{
            .service = this,
            .button_id = button.button_id,
            .generation = generation,
        };
        click_timers_[index] = xTimerCreate(
            "btn_click", kSingleClickDelayTicks, pdFALSE, deferred, SingleClickTimerCallback);
        if (click_timers_[index] == nullptr) {
            ESP_LOGW(TAG, "Failed to create single-click timer for '%s'", button.button_id.c_str());
            delete deferred;
            QueueButtonEvent(button.button_id, ButtonEvent::kSingleClick);
            return;
        }
    } else {
        auto* deferred = static_cast<DeferredClick*>(pvTimerGetTimerID(click_timers_[index]));
        if (deferred != nullptr) {
            deferred->button_id = button.button_id;
            deferred->generation = generation;
        }
        xTimerStop(click_timers_[index], 0);
    }

    if (xTimerChangePeriod(click_timers_[index], kSingleClickDelayTicks, 0) != pdPASS) {
        ESP_LOGW(TAG, "Failed to arm single-click timer for '%s'", button.button_id.c_str());
        QueueButtonEvent(button.button_id, ButtonEvent::kSingleClick);
    }
}

void ButtonBindingService::CancelSingleClick(const RegisteredButton& button) {
    const auto it = std::find_if(registered_.begin(), registered_.end(),
                                 [&button](const RegisteredButton& candidate) {
                                     return candidate.button_id == button.button_id;
                                 });
    if (it == registered_.end()) {
        return;
    }

    const size_t index = static_cast<size_t>(std::distance(registered_.begin(), it));
    if (index >= click_generations_.size() || index >= click_timers_.size()) {
        return;
    }

    ++click_generations_[index];
    if (click_timers_[index] != nullptr) {
        xTimerStop(click_timers_[index], 0);
    }
}

void ButtonBindingService::ExecuteDeferredSingleClick(std::string_view button_id,
                                                      uint32_t generation) {
    QueueButtonEvent(button_id, ButtonEvent::kSingleClick, generation, true);
}

bool ButtonBindingService::QueueButtonEvent(std::string_view button_id,
                                            ButtonEvent event,
                                            uint32_t generation,
                                            bool check_generation) {
    if (event_queue_ == nullptr) {
        return false;
    }

    QueuedButtonEvent queued;
    const size_t copy_len = std::min(button_id.size(), sizeof(queued.button_id) - 1);
    std::memcpy(queued.button_id, button_id.data(), copy_len);
    queued.button_id[copy_len] = '\0';
    queued.event = event;
    queued.generation = generation;
    queued.check_generation = check_generation;

    if (xQueueSend(event_queue_, &queued, 0) != pdPASS) {
        ESP_LOGW(TAG, "Button event queue full; dropped %.*s %s",
                 static_cast<int>(button_id.size()), button_id.data(), EventName(event));
        return false;
    }
    return true;
}

void ButtonBindingService::ProcessQueuedEvent(const QueuedButtonEvent& event) {
    const auto* button = FindRegistered(event.button_id);
    if (button == nullptr) {
        return;
    }

    if (event.check_generation) {
        const auto it = std::find_if(registered_.begin(), registered_.end(),
                                     [button](const RegisteredButton& candidate) {
                                         return candidate.button_id == button->button_id;
                                     });
        if (it == registered_.end()) {
            return;
        }

        const size_t index = static_cast<size_t>(std::distance(registered_.begin(), it));
        if (index >= click_generations_.size() || event.generation != click_generations_[index]) {
            ESP_LOGD(TAG, "Skipping stale single-click for '%s'", event.button_id);
            return;
        }
    }

    ExecuteBinding(*button, event.event);
}

void ButtonBindingService::ExecuteAction(const ButtonAction& action, const std::string& toast) {
    auto* pending = new PendingAction{
        .service = this,
        .action = action,
        .toast = toast,
    };

    bool queued = false;
    if (lvgl_port_lock(1000)) {
        queued = lv_async_call(RunPendingAction, pending) == LV_RESULT_OK;
        lvgl_port_unlock();
    }

    if (!queued) {
        ESP_LOGW(TAG, "Failed to queue button action");
        delete pending;
    }
}

ButtonAction ButtonBindingService::DefaultAction(std::string_view button_id,
                                                 ButtonEvent event) const {
    if (event == ButtonEvent::kSingleClick && button_id == "boot_button") {
        return ButtonAction{.type = ButtonActionType::kHome, .app_id = ""};
    }
    if (button_id == "io10_key_button") {
        if (event == ButtonEvent::kSingleClick) {
            return ButtonAction{.type = ButtonActionType::kToggleControlCenter, .app_id = ""};
        }
        if (event == ButtonEvent::kDoubleClick) {
            return ButtonAction{.type = ButtonActionType::kLaunchApp, .app_id = "smart"};
        }
        if (event == ButtonEvent::kLongPressStart) {
            return ButtonAction{.type = ButtonActionType::kLock, .app_id = ""};
        }
    }
    return ButtonAction{.type = ButtonActionType::kNone, .app_id = ""};
}

std::string ButtonBindingService::StorageKey(std::string_view button_id, ButtonEvent event) const {
    char key[16] = {};
    std::snprintf(key, sizeof(key), "b%08lx%c",
                  static_cast<unsigned long>(Fnv1a(button_id)), EventCode(event));
    return key;
}

const ButtonBindingService::RegisteredButton* ButtonBindingService::FindRegistered(
    std::string_view button_id) const {
    const std::string normalized = NormalizeId(button_id);
    const auto it = std::find_if(registered_.begin(), registered_.end(),
                                 [&normalized](const RegisteredButton& button) {
                                     return button.button_id == normalized;
                                 });
    return it == registered_.end() ? nullptr : &(*it);
}

const ButtonState* ButtonBindingService::FindButton(std::string_view button_id) const {
    const std::string normalized = NormalizeId(button_id);
    const auto it = std::find_if(buttons_.begin(), buttons_.end(),
                                 [&normalized](const ButtonState& button) {
                                     return button.id == normalized;
                                 });
    return it == buttons_.end() ? nullptr : &(*it);
}

void ButtonBindingService::ButtonEventCallback(const char* button_id,
                                               BoardButtonEvent event,
                                               void* user_data) {
    auto* service = static_cast<ButtonBindingService*>(user_data);
    if (service != nullptr) {
        service->HandleButtonEvent(button_id != nullptr ? button_id : "", ToButtonEvent(event));
    }
}

void ButtonBindingService::SingleClickTimerCallback(TimerHandle_t timer) {
    auto* deferred = static_cast<DeferredClick*>(pvTimerGetTimerID(timer));
    if (deferred != nullptr && deferred->service != nullptr) {
        deferred->service->ExecuteDeferredSingleClick(deferred->button_id, deferred->generation);
    }
}

void ButtonBindingService::WorkerTask(void* user_data) {
    auto* service = static_cast<ButtonBindingService*>(user_data);
    if (service == nullptr || service->event_queue_ == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    QueuedButtonEvent event;
    while (true) {
        if (xQueueReceive(service->event_queue_, &event, portMAX_DELAY) == pdPASS) {
            service->ProcessQueuedEvent(event);
        }
    }
}

void ButtonBindingService::RunPendingAction(void* user_data) {
    auto* pending = static_cast<PendingAction*>(user_data);
    if (pending == nullptr) {
        return;
    }

    auto* service = pending->service;
    if (service != nullptr && service->navigation_ != nullptr) {
        bool ok = false;
        switch (pending->action.type) {
            case ButtonActionType::kHome:
                ok = service->navigation_->ReturnHome();
                break;
            case ButtonActionType::kLock:
                ok = service->navigation_->Lock();
                break;
            case ButtonActionType::kToggleControlCenter:
                ok = service->navigation_->ToggleControlCenter();
                break;
            case ButtonActionType::kLaunchApp:
                ok = service->navigation_->Launch(pending->action.app_id);
                break;
            case ButtonActionType::kNone:
            default:
                ok = true;
                break;
        }
        if (service->ui_ != nullptr) {
            service->ui_->ShowToastUnlocked(ok ? pending->toast.c_str() : "Button action failed");
        }
    }

    delete pending;
}

std::string ButtonBindingService::MakeTitle(std::string_view id) {
    if (id == "boot_button") {
        return "IO0 Boot";
    }
    if (id == "io10_key_button") {
        return "IO10 Key";
    }
    return TitleCase(id);
}

}  // namespace rodakos
