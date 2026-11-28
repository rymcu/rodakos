#pragma once

#include "phone_os/inbox_model.h"
#include "phone_os/phone_app.h"

#include <cstdint>
#include <string>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class InboxService;
}

// Message center: notices and todo reminders the Rodak server recorded for this device. The
// device can read, complete a todo and snooze the current reminder; creating and editing todos
// stays with voice and the desktop client.
class MessagesApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    bool OnBackRequested() override;
    void OnScreenShareStarting() override { Refresh(true); }

    // Visible for host tests: rebuild from the service view if its revision changed.
    void Refresh(bool force = false);

    enum class Filter : uint8_t {
        kAll,
        kTodo,
        kNotice,
    };

private:
    static void RefreshTimerCallback(lv_timer_t* timer);

    void CreateUi();
    void DestroyUi();
    void BuildList();
    void BuildDetail(const rodakos::InboxItem& item);
    void BuildPrivacyCover();
    void BuildStatusPage(const char* title, const char* message);
    void ClearContent();
    void OpenItem(const std::string& notice_id);
    void CloseDetail();
    void UpdateHeader();
    bool CanWrite(const rodakos::InboxItem& item) const;
    lv_coord_t ContentHeight() const;
    void NavigateHome();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::InboxService* inbox_ = nullptr;
    rodakos::InboxView view_;
    uint32_t shown_revision_ = UINT32_MAX;
    bool shown_share_ = false;
    bool shown_snooze_menu_ = false;
    Filter filter_ = Filter::kAll;
    std::string selected_id_;
    bool snooze_menu_open_ = false;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
};

void RegisterMessagesApp(PhoneAppRegistry& registry);
