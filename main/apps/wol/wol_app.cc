#include "apps/wol/wol_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/wake_on_lan_service.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/wifi_adapter.h"

#include <esp_log.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>

namespace {
constexpr const char* TAG = "WolApp";

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().ReturnHome();
    }
}

std::string Trim(std::string value) {
    const auto is_space = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [&](char character) {
                                                return !is_space(static_cast<unsigned char>(character));
                                            }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](char character) {
                                 return !is_space(static_cast<unsigned char>(character));
                             }).base(),
                value.end());
    return value;
}

size_t DeviceIndexFromButton(lv_obj_t* button) {
    return static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button))) - 1;
}

const char* ValidationMessage(rodakos::WolDeviceValidationStatus status) {
    switch (status) {
        case rodakos::WolDeviceValidationStatus::kOk:
            return "Device saved";
        case rodakos::WolDeviceValidationStatus::kInvalidName:
            return "Enter a device name";
        case rodakos::WolDeviceValidationStatus::kInvalidMac:
            return "Invalid unicast MAC address";
        case rodakos::WolDeviceValidationStatus::kInvalidBroadcastAddress:
            return "Invalid broadcast address";
        case rodakos::WolDeviceValidationStatus::kInvalidPort:
            return "Port must be 1-65535";
    }
    return "Invalid device";
}

const char* SendResultMessage(rodakos::WakeOnLanResult result) {
    switch (result) {
        case rodakos::WakeOnLanResult::kSent:
            return "Magic packet sent";
        case rodakos::WakeOnLanResult::kWifiDisconnected:
            return "Connect WiFi first";
        case rodakos::WakeOnLanResult::kInvalidMac:
            return "Invalid MAC address";
        case rodakos::WakeOnLanResult::kInvalidBroadcastAddress:
            return "Invalid broadcast address";
        case rodakos::WakeOnLanResult::kInvalidPort:
            return "Invalid UDP port";
        case rodakos::WakeOnLanResult::kSocketOpenFailed:
        case rodakos::WakeOnLanResult::kSocketConfigureFailed:
        case rodakos::WakeOnLanResult::kSendFailed:
            return "Magic packet failed";
    }
    return "Magic packet failed";
}

}  // namespace

bool WolApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    wake_on_lan_ = context.services().wake_on_lan();

    const rodakos::WolDeviceLoadResult loaded = store_.Load();
    devices_ = loaded.devices;
    storage_write_allowed_ = loaded.write_allowed;
    storage_reset_available_ = store_.reset_allowed();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }
    CreateUi();
    RebuildDeviceList();
    UpdateConnectionStatus();
    if (!storage_write_allowed_ && !storage_reset_available_) {
        ui_->ShowToastUnlocked("Saved devices are read-only");
    }
    ESP_LOGI(TAG, "Wake app created with %u devices", static_cast<unsigned>(devices_.size()));
    return true;
}

void WolApp::OnResume() {
    if (ui_ == nullptr) {
        return;
    }
    PhoneUiLock lock(*ui_);
    if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
        UpdateConnectionStatus();
    }
}

void WolApp::OnPause() {
    if (ui_ == nullptr) {
        return;
    }
    PhoneUiLock lock(*ui_);
    if (lock.locked()) {
        soft_keyboard_.Hide();
        if (root_ != nullptr && lv_obj_is_valid(root_)) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void WolApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            soft_keyboard_.Hide();
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }

    root_ = nullptr;
    connection_label_ = nullptr;
    add_button_ = nullptr;
    device_list_ = nullptr;
    empty_state_ = nullptr;
    editor_ = nullptr;
    name_input_ = nullptr;
    mac_input_ = nullptr;
    broadcast_input_ = nullptr;
    port_input_ = nullptr;
    delete_button_label_ = nullptr;
    devices_.clear();
    context_ = nullptr;
    ui_ = nullptr;
    wake_on_lan_ = nullptr;
}

void WolApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Wake", [](lv_event_t* event) {
        static_cast<WolApp*>(lv_event_get_user_data(event))->NavigateHome();
    }, [](lv_event_t* event) {
        static_cast<WolApp*>(lv_event_get_user_data(event))->NavigateHome();
    }, this);

    connection_label_ = lv_label_create(root_);
    lv_obj_set_pos(connection_label_, 10, 48);
    lv_obj_set_size(connection_label_, 232, 24);
    lv_obj_set_style_text_font(connection_label_, &phone_font_12, 0);
    lv_label_set_long_mode(connection_label_, LV_LABEL_LONG_DOT);

    add_button_ = PhoneCreateTextButton(*ui_, root_, "Add", 58, 28);
    lv_obj_set_pos(add_button_, 252, 43);
    lv_obj_add_event_cb(add_button_, [](lv_event_t* event) {
        auto* self = static_cast<WolApp*>(lv_event_get_user_data(event));
        if (self->storage_reset_available_) {
            if (!self->store_.Reset()) {
                self->storage_reset_available_ = self->store_.reset_allowed();
                self->ui_->ShowToastUnlocked("Resetting devices failed");
                self->UpdateConnectionStatus();
                return;
            }
            self->devices_.clear();
            self->storage_write_allowed_ = true;
            self->storage_reset_available_ = false;
            self->RebuildDeviceList();
            self->UpdateConnectionStatus();
            self->ui_->ShowToastUnlocked("Saved devices reset");
        } else if (!self->storage_write_allowed_) {
            self->ui_->ShowToastUnlocked("Saved devices are read-only");
        } else if (self->devices_.size() >= rodakos::kWolMaxDevices) {
            self->ui_->ShowToastUnlocked("Device limit reached");
        } else {
            self->ShowEditor(kNewDeviceIndex);
        }
    }, LV_EVENT_CLICKED, this);

    device_list_ = lv_obj_create(root_);
    lv_obj_remove_style_all(device_list_);
    lv_obj_set_size(device_list_, 320, 162);
    lv_obj_set_pos(device_list_, 0, 78);
    lv_obj_set_style_bg_opa(device_list_, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(device_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(device_list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(device_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(device_list_, 10, 0);
    lv_obj_set_style_pad_row(device_list_, 6, 0);
}

void WolApp::RebuildDeviceList() {
    if (device_list_ == nullptr) {
        return;
    }
    lv_obj_clean(device_list_);
    empty_state_ = nullptr;

    if (devices_.empty()) {
        empty_state_ = lv_label_create(device_list_);
        lv_label_set_text(empty_state_, "No devices");
        lv_obj_set_size(empty_state_, 300, 80);
        lv_obj_set_style_text_font(empty_state_, &phone_font_14, 0);
        lv_obj_set_style_text_color(empty_state_, rodakos_theme_text_secondary(), 0);
        lv_obj_set_style_text_align(empty_state_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(empty_state_, 30, 0);
        return;
    }

    for (size_t index = 0; index < devices_.size(); ++index) {
        CreateDeviceRow(index);
    }
}

void WolApp::CreateDeviceRow(size_t index) {
    const rodakos::WolDevice& device = devices_[index];
    auto* row = PhoneCreateCard(*ui_, device_list_, 300, 62);
    lv_obj_set_style_pad_all(row, 8, 0);

    auto* name = lv_label_create(row);
    lv_label_set_text(name, device.name.c_str());
    lv_obj_set_pos(name, 0, 0);
    lv_obj_set_size(name, 190, 20);
    lv_obj_set_style_text_font(name, &phone_font_14, 0);
    lv_obj_set_style_text_color(name, rodakos_theme_text_primary(), 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    auto* address = lv_label_create(row);
    lv_label_set_text_fmt(address, "%s  %s:%u", device.mac_address.c_str(),
                          device.broadcast_address.c_str(), static_cast<unsigned>(device.port));
    lv_obj_set_pos(address, 0, 27);
    lv_obj_set_size(address, 202, 18);
    lv_obj_set_style_text_font(address, &phone_font_12, 0);
    lv_obj_set_style_text_color(address, rodakos_theme_text_secondary(), 0);
    lv_label_set_long_mode(address, LV_LABEL_LONG_DOT);

    auto* wake_button = RodakosCreateHeaderIconButton(row, FONT_AWESOME_POWER_OFF);
    lv_obj_set_size(wake_button, 38, 38);
    lv_obj_align(wake_button, LV_ALIGN_RIGHT_MID, -46, 0);
    lv_obj_set_style_bg_color(wake_button, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(wake_button, 0), lv_color_white(), 0);
    lv_obj_set_user_data(wake_button, reinterpret_cast<void*>(static_cast<uintptr_t>(index + 1)));
    lv_obj_add_event_cb(wake_button, [](lv_event_t* event) {
        auto* self = static_cast<WolApp*>(lv_event_get_user_data(event));
        self->WakeDevice(DeviceIndexFromButton(static_cast<lv_obj_t*>(lv_event_get_target(event))));
    }, LV_EVENT_CLICKED, this);

    auto* edit_button = RodakosCreateHeaderIconButton(row, FONT_AWESOME_PEN_TO_SQUARE);
    lv_obj_set_size(edit_button, 38, 38);
    lv_obj_align(edit_button, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_user_data(edit_button, reinterpret_cast<void*>(static_cast<uintptr_t>(index + 1)));
    lv_obj_add_event_cb(edit_button, [](lv_event_t* event) {
        auto* self = static_cast<WolApp*>(lv_event_get_user_data(event));
        self->ShowEditor(DeviceIndexFromButton(static_cast<lv_obj_t*>(lv_event_get_target(event))));
    }, LV_EVENT_CLICKED, this);
}

lv_obj_t* WolApp::CreateEditorField(lv_obj_t* parent,
                                    const char* label,
                                    const char* placeholder,
                                    const char* value,
                                    size_t max_length,
                                    lv_coord_t y,
                                    const char* accepted_characters) {
    auto* caption = lv_label_create(parent);
    lv_label_set_text(caption, label);
    lv_obj_set_pos(caption, 4, y);
    lv_obj_set_style_text_font(caption, &phone_font_12, 0);
    lv_obj_set_style_text_color(caption, rodakos_theme_text_secondary(), 0);

    auto* input = lv_textarea_create(parent);
    lv_obj_set_size(input, 284, 32);
    lv_obj_set_pos(input, 0, y + 16);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, max_length);
    lv_textarea_set_text(input, value);
    lv_textarea_set_placeholder_text(input, placeholder);
    if (accepted_characters != nullptr) {
        lv_textarea_set_accepted_chars(input, accepted_characters);
    }
    lv_obj_set_style_bg_color(input, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(input, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(input, &phone_font_12, 0);
    lv_obj_set_style_border_width(input, 1, 0);
    lv_obj_set_style_border_color(input, rodakos_theme_primary(), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(input, 6, 0);
    lv_obj_add_event_cb(input, [](lv_event_t* event) {
        auto* self = static_cast<WolApp*>(lv_event_get_user_data(event));
        auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
        self->soft_keyboard_.Show(target);
        lv_obj_scroll_to_view_recursive(target, LV_ANIM_ON);
    }, LV_EVENT_FOCUSED, this);
    return input;
}

void WolApp::ShowEditor(size_t index) {
    if (editor_ != nullptr || (index != kNewDeviceIndex && index >= devices_.size())) {
        return;
    }
    editing_index_ = index;
    delete_armed_ = false;
    const rodakos::WolDevice device = index == kNewDeviceIndex ? rodakos::WolDevice{} : devices_[index];

    editor_ = lv_obj_create(root_);
    lv_obj_remove_style_all(editor_);
    lv_obj_set_size(editor_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(editor_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(editor_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(editor_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(editor_);

    auto* toolbar = lv_obj_create(editor_);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, 320, 40);
    lv_obj_set_style_bg_color(toolbar, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);

    auto* cancel = RodakosCreateHeaderIconButton(toolbar, FONT_AWESOME_XMARK);
    lv_obj_align(cancel, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_add_event_cb(cancel, [](lv_event_t* event) {
        static_cast<WolApp*>(lv_event_get_user_data(event))->CloseEditor();
    }, LV_EVENT_CLICKED, this);

    auto* title = lv_label_create(toolbar);
    lv_label_set_text(title, index == kNewDeviceIndex ? "Add device" : "Edit device");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_set_style_text_color(title, rodakos_theme_text_primary(), 0);
    lv_obj_center(title);

    auto* save = RodakosCreateHeaderIconButton(toolbar, FONT_AWESOME_CHECK);
    lv_obj_align(save, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(save, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(save, 0), lv_color_white(), 0);
    lv_obj_add_event_cb(save, [](lv_event_t* event) {
        static_cast<WolApp*>(lv_event_get_user_data(event))->SaveEditor();
    }, LV_EVENT_CLICKED, this);

    auto* form = lv_obj_create(editor_);
    lv_obj_remove_style_all(form);
    lv_obj_set_size(form, 304, 194);
    lv_obj_set_pos(form, 8, 42);
    lv_obj_set_style_bg_opa(form, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(form, 4, 0);
    lv_obj_set_scroll_dir(form, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(form, LV_SCROLLBAR_MODE_AUTO);

    name_input_ = CreateEditorField(form, "Name", "Office PC", device.name.c_str(),
                                    rodakos::kWolDeviceNameMaxBytes, 0);
    mac_input_ = CreateEditorField(form, "MAC", "AA:BB:CC:DD:EE:FF",
                                   device.mac_address.c_str(), 17, 52,
                                   "0123456789abcdefABCDEF:-");
    broadcast_input_ = CreateEditorField(form, "Broadcast", "255.255.255.255",
                                         device.broadcast_address.c_str(), 15, 104,
                                         "0123456789.");
    const std::string port = std::to_string(device.port);
    port_input_ = CreateEditorField(form, "UDP port", "9", port.c_str(), 5, 156,
                                    "0123456789");

    if (index != kNewDeviceIndex) {
        auto* delete_button = PhoneCreateTextButton(*ui_, form, "Delete device", 284, 32);
        lv_obj_set_pos(delete_button, 0, 214);
        lv_obj_set_style_bg_color(delete_button, rodakos_theme_error(), 0);
        delete_button_label_ = lv_obj_get_child(delete_button, 0);
        lv_obj_add_event_cb(delete_button, [](lv_event_t* event) {
            static_cast<WolApp*>(lv_event_get_user_data(event))->DeleteEditorDevice();
        }, LV_EVENT_CLICKED, this);
    }
}

void WolApp::CloseEditor() {
    soft_keyboard_.Hide();
    if (editor_ != nullptr && lv_obj_is_valid(editor_)) {
        lv_obj_delete(editor_);
    }
    editor_ = nullptr;
    name_input_ = nullptr;
    mac_input_ = nullptr;
    broadcast_input_ = nullptr;
    port_input_ = nullptr;
    delete_button_label_ = nullptr;
    editing_index_ = kNewDeviceIndex;
    delete_armed_ = false;
}

void WolApp::SaveEditor() {
    if (!storage_write_allowed_ || name_input_ == nullptr || mac_input_ == nullptr ||
        broadcast_input_ == nullptr || port_input_ == nullptr) {
        ui_->ShowToastUnlocked("Saved devices are read-only");
        return;
    }

    const std::string port_text = Trim(lv_textarea_get_text(port_input_));
    char* port_end = nullptr;
    const unsigned long port_value = std::strtoul(port_text.c_str(), &port_end, 10);
    const uint16_t parsed_port =
        port_end != port_text.c_str() && *port_end == '\0' && port_value <= 65535
            ? static_cast<uint16_t>(port_value)
            : static_cast<uint16_t>(0);
    rodakos::WolDevice device{
        .name = Trim(lv_textarea_get_text(name_input_)),
        .mac_address = Trim(lv_textarea_get_text(mac_input_)),
        .broadcast_address = Trim(lv_textarea_get_text(broadcast_input_)),
        .port = parsed_port,
    };
    const rodakos::WolDeviceValidationStatus validation = rodakos::NormalizeWolDevice(&device);
    if (validation != rodakos::WolDeviceValidationStatus::kOk) {
        ui_->ShowToastUnlocked(ValidationMessage(validation));
        return;
    }

    for (size_t index = 0; index < devices_.size(); ++index) {
        if (index != editing_index_ && devices_[index].mac_address == device.mac_address) {
            ui_->ShowToastUnlocked("MAC address already exists");
            return;
        }
    }

    std::vector<rodakos::WolDevice> candidate = devices_;
    if (editing_index_ == kNewDeviceIndex) {
        if (candidate.size() >= rodakos::kWolMaxDevices) {
            ui_->ShowToastUnlocked("Device limit reached");
            return;
        }
        candidate.push_back(std::move(device));
    } else if (editing_index_ < candidate.size()) {
        candidate[editing_index_] = std::move(device);
    } else {
        return;
    }

    if (!store_.Save(candidate)) {
        storage_write_allowed_ = store_.write_allowed();
        ui_->ShowToastUnlocked("Saving device failed");
        return;
    }
    devices_ = std::move(candidate);
    CloseEditor();
    RebuildDeviceList();
    ui_->ShowToastUnlocked("Device saved");
}

void WolApp::DeleteEditorDevice() {
    if (editing_index_ >= devices_.size() || !storage_write_allowed_) {
        return;
    }
    if (!delete_armed_) {
        delete_armed_ = true;
        if (delete_button_label_ != nullptr) {
            lv_label_set_text(delete_button_label_, "Confirm delete");
        }
        return;
    }

    std::vector<rodakos::WolDevice> candidate = devices_;
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(editing_index_));
    if (!store_.Save(candidate)) {
        storage_write_allowed_ = store_.write_allowed();
        ui_->ShowToastUnlocked("Deleting device failed");
        return;
    }
    devices_ = std::move(candidate);
    CloseEditor();
    RebuildDeviceList();
    ui_->ShowToastUnlocked("Device deleted");
}

void WolApp::WakeDevice(size_t index) {
    if (index >= devices_.size()) {
        return;
    }
    if (wake_on_lan_ == nullptr) {
        ui_->ShowToastUnlocked("Wake service unavailable");
        return;
    }
    const rodakos::WolDevice& device = devices_[index];
    const rodakos::WakeOnLanResult result = wake_on_lan_->Send(
        device.mac_address, device.broadcast_address, device.port);
    ui_->ShowToastUnlocked(SendResultMessage(result));
    UpdateConnectionStatus();
}

void WolApp::UpdateConnectionStatus() {
    if (connection_label_ == nullptr || context_ == nullptr) {
        return;
    }
    WiFiAdapter* wifi = context_->services().wifi();
    const bool connected = wifi != nullptr && wifi->GetStatus() == WiFiStatus::kConnected;
    if (connected) {
        lv_label_set_text_fmt(connection_label_, FONT_AWESOME_WIFI "  %s",
                              wifi->GetConnectedSSID().c_str());
        lv_obj_set_style_text_color(connection_label_, rodakos_theme_success(), 0);
    } else {
        lv_label_set_text(connection_label_, FONT_AWESOME_WIFI_SLASH "  WiFi disconnected");
        lv_obj_set_style_text_color(connection_label_, rodakos_theme_warning(), 0);
    }
    if (add_button_ != nullptr) {
        auto* add_label = lv_obj_get_child(add_button_, 0);
        if (add_label != nullptr) {
            lv_label_set_text(add_label, storage_reset_available_ ? "Reset" : "Add");
        }
        const bool disabled = (!storage_write_allowed_ && !storage_reset_available_) ||
                              (!storage_reset_available_ && devices_.size() >= rodakos::kWolMaxDevices);
        if (disabled) {
            lv_obj_add_state(add_button_, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(add_button_, LV_STATE_DISABLED);
        }
    }
}

void WolApp::NavigateHome() {
    lv_async_call(DeferReturnHome, context_);
}

void RegisterWolApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "wol",
        .title = "Wake",
        .icon = FONT_AWESOME_POWER_OFF,
        .category = PhoneAppCategory::kTools,
        .capabilities = PhoneCapability::kNetwork,
        .show_on_home = true,
        .aliases = {"wake", "wake on lan", "network wake", "网络唤醒", "唤醒"},
        .create = []() { return std::make_unique<WolApp>(); },
    });
}
