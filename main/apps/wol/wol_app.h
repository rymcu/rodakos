#pragma once

#include "apps/wol/wol_device_store.h"
#include "phone_os/phone_app.h"
#include "phone_ui/soft_keyboard.h"

#include <cstddef>
#include <vector>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class WakeOnLanService;
}

class WolApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;

private:
    static constexpr size_t kNewDeviceIndex = static_cast<size_t>(-1);

    void CreateUi();
    void RebuildDeviceList();
    void CreateDeviceRow(size_t index);
    void ShowEditor(size_t index);
    void CloseEditor();
    void SaveEditor();
    void DeleteEditorDevice();
    void WakeDevice(size_t index);
    void NavigateHome();
    void UpdateConnectionStatus();
    lv_obj_t* CreateEditorField(lv_obj_t* parent,
                                const char* label,
                                const char* placeholder,
                                const char* value,
                                size_t max_length,
                                lv_coord_t y,
                                const char* accepted_characters = nullptr);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::WakeOnLanService* wake_on_lan_ = nullptr;
    rodakos::WolDeviceStore store_;
    std::vector<rodakos::WolDevice> devices_;
    bool storage_write_allowed_ = false;
    bool storage_reset_available_ = false;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* connection_label_ = nullptr;
    lv_obj_t* add_button_ = nullptr;
    lv_obj_t* device_list_ = nullptr;
    lv_obj_t* empty_state_ = nullptr;

    lv_obj_t* editor_ = nullptr;
    lv_obj_t* name_input_ = nullptr;
    lv_obj_t* mac_input_ = nullptr;
    lv_obj_t* broadcast_input_ = nullptr;
    lv_obj_t* port_input_ = nullptr;
    lv_obj_t* delete_button_label_ = nullptr;
    size_t editing_index_ = kNewDeviceIndex;
    bool delete_armed_ = false;
    SoftKeyboard soft_keyboard_;
};

void RegisterWolApp(PhoneAppRegistry& registry);
