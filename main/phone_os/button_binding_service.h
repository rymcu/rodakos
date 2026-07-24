#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "rodakos_adapters/board_device_adapter.h"

class PhoneNavigation;
class PhoneUi;

namespace rodakos {

enum class ButtonEvent : uint8_t {
    kSingleClick,
    kDoubleClick,
    kLongPressStart,
};

inline constexpr std::array<ButtonEvent, 3> kButtonEvents = {
    ButtonEvent::kSingleClick,
    ButtonEvent::kDoubleClick,
    ButtonEvent::kLongPressStart,
};

enum class ButtonActionType {
    kNone,
    kHome,
    kLock,
    kToggleControlCenter,
    kLaunchApp,
};

struct ButtonAction {
    ButtonActionType type = ButtonActionType::kNone;
    std::string app_id;
};

struct ButtonBinding {
    std::string button_id;
    std::string title;
    ButtonEvent event = ButtonEvent::kSingleClick;
    ButtonAction action;
    bool custom = false;
};

struct ButtonState {
    std::string id;
    std::string title;
    std::string device_name;
    uint8_t physical_index = 0;
    bool available = false;
};

class ButtonBindingService {
public:
    bool Init(PhoneNavigation& navigation, PhoneUi& ui);
    bool IsAvailable() const { return !buttons_.empty(); }

    const std::vector<ButtonState>& ListButtons() const { return buttons_; }
    std::vector<ButtonBinding> ListBindings() const;

    const ButtonState* GetButton(std::string_view button_id) const;
    ButtonBinding GetBinding(std::string_view button_id, ButtonEvent event) const;
    bool SetBinding(std::string_view button_id, ButtonEvent event, const ButtonAction& action);
    bool ResetBinding(std::string_view button_id, ButtonEvent event);

    static const char* EventName(ButtonEvent event);
    static const char* EventLabel(ButtonEvent event);
    static std::string EncodeAction(const ButtonAction& action);
    static ButtonAction DecodeAction(std::string_view encoded);
    static std::string ActionLabel(const ButtonAction& action);

private:
    struct RegisteredButton {
        std::string button_id;
        std::string title;
        std::string device_name;
        uint8_t physical_index = 0;
    };

    struct PendingAction {
        ButtonBindingService* service = nullptr;
        ButtonAction action;
        std::string toast;
    };

    struct DeferredClick {
        ButtonBindingService* service = nullptr;
        std::string button_id;
        uint32_t generation = 0;
    };

    struct QueuedButtonEvent {
        char button_id[32] = {};
        ButtonEvent event = ButtonEvent::kSingleClick;
        uint32_t generation = 0;
        bool check_generation = false;
    };

    void DiscoverButtons();
    void RegisterCallbacks();
    bool StartWorker();
    void ResetDiscoveredState();
    void HandleButtonEvent(std::string_view button_id, ButtonEvent event);
    void ExecuteBinding(const RegisteredButton& button, ButtonEvent event);
    void ScheduleSingleClick(const RegisteredButton& button);
    void CancelSingleClick(const RegisteredButton& button);
    void ExecuteDeferredSingleClick(std::string_view button_id, uint32_t generation);
    bool QueueButtonEvent(std::string_view button_id,
                          ButtonEvent event,
                          uint32_t generation = 0,
                          bool check_generation = false);
    void ProcessQueuedEvent(const QueuedButtonEvent& event);
    void ExecuteAction(const ButtonAction& action, const std::string& toast);
    ButtonAction DefaultAction(std::string_view button_id, ButtonEvent event) const;
    std::string StorageKey(std::string_view button_id, ButtonEvent event) const;
    const RegisteredButton* FindRegistered(std::string_view button_id) const;
    const ButtonState* FindButton(std::string_view button_id) const;

    static void ButtonEventCallback(const char* button_id, BoardButtonEvent event, void* user_data);
    static void SingleClickTimerCallback(TimerHandle_t timer);
    static void WorkerTask(void* user_data);
    static void RunPendingAction(void* user_data);
    static std::string MakeTitle(std::string_view id);

    PhoneNavigation* navigation_ = nullptr;
    PhoneUi* ui_ = nullptr;
    std::vector<ButtonState> buttons_;
    std::vector<RegisteredButton> registered_;
    std::vector<BoardButtonDevice> board_buttons_;
    std::vector<uint32_t> click_generations_;
    std::vector<TimerHandle_t> click_timers_;
    QueueHandle_t event_queue_ = nullptr;
    TaskHandle_t worker_task_ = nullptr;
    bool initialized_ = false;
};

}  // namespace rodakos
