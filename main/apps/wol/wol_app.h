#pragma once

#include "apps/wol/wol_device_store.h"
#include "phone_os/phone_app.h"
#include "phone_ui/soft_keyboard.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class WakeOnLanService;
}

// Wake-on-LAN. List page: Wi-Fi strip, device cards (left opens the editor, right sends the magic
// packet). Editor: a grouped form; each row opens a full-screen single-field input above the
// soft keyboard. Back goes input -> form -> list -> Home.
class WolApp final : public PhoneApp {
public:
    static constexpr uint32_t kSentFeedbackMs = 2000;

    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    bool OnBackRequested() override;

private:
    static constexpr size_t kNewDeviceIndex = static_cast<size_t>(-1);
    static constexpr size_t kNoDevice = static_cast<size_t>(-1);

    enum class Field { kName, kMac, kBroadcast, kPort };

    void CreateUi();
    void DestroyUi();
    void RebuildDeviceList();
    void CreateDeviceCard(size_t index, lv_coord_t y);
    void UpdateStrip();
    void UpdateHeaderAction();
    void ShowEditor(size_t index);
    void RefreshEditor();
    void CloseEditor();
    void ShowInput(Field field);
    void CloseInput();
    void SubmitInput();
    void SaveDraft();
    void AskDelete();
    void AskReset();
    void CloseDialog();
    void ConfirmDialog();
    void WakeDevice(size_t index);
    void ClearSentFeedback();
    void NavigateHome();
    void OpenWiFiSettings();
    bool WifiConnected() const;

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::WakeOnLanService* wake_on_lan_ = nullptr;
    rodakos::WolDeviceStore store_;
    rodakos::WolDeviceLoadStatus load_status_ = rodakos::WolDeviceLoadStatus::kMissing;
    std::vector<rodakos::WolDevice> devices_;
    bool storage_write_allowed_ = false;
    bool storage_reset_available_ = false;

    // Editor state: the device being edited and a working copy of its fields.
    size_t editing_index_ = kNoDevice;
    rodakos::WolDevice draft_;
    Field input_field_ = Field::kName;
    bool dialog_is_reset_ = false;
    size_t sent_index_ = kNoDevice;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* add_button_ = nullptr;
    lv_obj_t* strip_ = nullptr;
    lv_obj_t* strip_dot_ = nullptr;
    lv_obj_t* strip_label_ = nullptr;
    lv_obj_t* strip_right_ = nullptr;
    lv_obj_t* device_list_ = nullptr;
    lv_obj_t* status_box_ = nullptr;

    lv_obj_t* editor_ = nullptr;
    lv_obj_t* editor_title_ = nullptr;
    lv_obj_t* editor_values_[4] = {};
    lv_obj_t* editor_hint_ = nullptr;
    lv_obj_t* editor_delete_ = nullptr;

    lv_obj_t* input_ = nullptr;
    lv_obj_t* input_textarea_ = nullptr;
    lv_obj_t* input_hint_ = nullptr;

    lv_obj_t* dialog_ = nullptr;
    lv_timer_t* sent_timer_ = nullptr;
    SoftKeyboard soft_keyboard_;
};

void RegisterWolApp(PhoneAppRegistry& registry);
