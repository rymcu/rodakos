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
constexpr lv_coord_t kStripHeight = 24;
constexpr lv_coord_t kListTop = kRodakosAppHeaderHeight + kStripHeight;
constexpr lv_coord_t kCardHeight = 52;
constexpr lv_coord_t kCardGap = 6;
constexpr lv_coord_t kFormRowHeight = 36;

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) context->navigation().ReturnHome();
}

void DeferOpenWiFi(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) context->navigation().LaunchFrom("settings", "wol", "wifi");
}

std::string Trim(std::string value) {
    const auto not_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

lv_obj_t* CreatePlain(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* CreateFilledButton(lv_obj_t* parent, const char* icon, const char* text, lv_coord_t w, lv_coord_t h,
                             lv_color_t bg, lv_color_t fg) {
    auto* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, bg, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_darken(bg, 40), LV_STATE_PRESSED);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button, 6, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    if (icon != nullptr) CreateText(button, icon, PhoneIconFont(), fg);
    if (text != nullptr) CreateText(button, text, &phone_font_14, fg);
    return button;
}

void SetHidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) return;
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

const char* ValidationMessage(rodakos::WolDeviceValidationStatus status) {
    switch (status) {
        case rodakos::WolDeviceValidationStatus::kOk: return "";
        case rodakos::WolDeviceValidationStatus::kInvalidName: return "请填写设备名称";
        case rodakos::WolDeviceValidationStatus::kInvalidMac: return "MAC 地址无效，需 12 位十六进制";
        case rodakos::WolDeviceValidationStatus::kInvalidBroadcastAddress: return "广播地址无效";
        case rodakos::WolDeviceValidationStatus::kInvalidPort: return "端口范围 1-65535";
    }
    return "设备信息无效";
}

const char* SendResultMessage(rodakos::WakeOnLanResult result) {
    switch (result) {
        case rodakos::WakeOnLanResult::kSent: return "已发送唤醒包";
        case rodakos::WakeOnLanResult::kWifiDisconnected: return "请先连接 Wi-Fi";
        case rodakos::WakeOnLanResult::kInvalidMac: return "MAC 地址无效";
        case rodakos::WakeOnLanResult::kInvalidBroadcastAddress: return "广播地址无效";
        case rodakos::WakeOnLanResult::kInvalidPort: return "UDP 端口无效";
        case rodakos::WakeOnLanResult::kSocketOpenFailed:
        case rodakos::WakeOnLanResult::kSocketConfigureFailed:
        case rodakos::WakeOnLanResult::kSendFailed: return "唤醒包发送失败";
    }
    return "唤醒包发送失败";
}

const char* FieldTitle(int field) {
    static const char* kTitles[] = {"名称", "MAC 地址", "广播地址", "UDP 端口"};
    return kTitles[field];
}

const char* FieldHint(int field) {
    static const char* kHints[] = {"最多 32 字节，例如 书房台式机", "12 位十六进制，支持 : 或 - 分隔",
                                   "通常为 255.255.255.255", "范围 1-65535，默认 9"};
    return kHints[field];
}

}  // namespace

bool WolApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    wake_on_lan_ = context.services().wake_on_lan();

    const rodakos::WolDeviceLoadResult loaded = store_.Load();
    load_status_ = loaded.status;
    devices_ = loaded.devices;
    storage_write_allowed_ = loaded.write_allowed;
    storage_reset_available_ = store_.reset_allowed();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;
    CreateUi();
    ESP_LOGI(TAG, "Wake app created with %u devices", static_cast<unsigned>(devices_.size()));
    return true;
}

void WolApp::OnResume() {
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
        UpdateStrip();
        RebuildDeviceList();
    }
}

void WolApp::OnPause() {
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (lock.locked()) {
        soft_keyboard_.Hide();
        if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void WolApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (context_ != nullptr) {
                lv_async_call_cancel(DeferReturnHome, context_);
                lv_async_call_cancel(DeferOpenWiFi, context_);
            }
            DestroyUi();
        }
    }
    devices_.clear();
    context_ = nullptr;
    ui_ = nullptr;
    wake_on_lan_ = nullptr;
}

bool WolApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;
    const bool hidden = root_ != nullptr && lv_obj_is_valid(root_) && lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    const size_t editing = editing_index_;
    const rodakos::WolDevice draft = draft_;
    DestroyUi();
    CreateUi();
    // Keep an open editor (and its unsaved draft) across the rebuild.
    if (editing != kNoDevice) {
        ShowEditor(editing);
        draft_ = draft;
        RefreshEditor();
    }
    if (hidden && root_ != nullptr) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    return true;
}

bool WolApp::OnBackRequested() {
    if (dialog_ != nullptr) {
        CloseDialog();
    } else if (input_ != nullptr) {
        CloseInput();
    } else if (editor_ != nullptr) {
        CloseEditor();
    } else {
        NavigateHome();
    }
    return true;
}

void WolApp::CreateUi() {
    root_ = CreatePlain(ui_->screen());
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    CreateAppHeader(root_, "网络唤醒", [](lv_event_t* e) {
        static_cast<WolApp*>(lv_event_get_user_data(e))->OnBackRequested();
    }, [](lv_event_t* e) {
        auto* self = static_cast<WolApp*>(lv_event_get_user_data(e));
        if (self->storage_write_allowed_ && self->devices_.size() < rodakos::kWolMaxDevices) {
            self->ShowEditor(kNewDeviceIndex);
        } else if (!self->storage_write_allowed_) {
            self->ui_->ShowToastUnlocked("设备列表只读");
        } else {
            self->ui_->ShowToastUnlocked("最多保存 8 台设备");
        }
    }, this, &title_label_, ICON_ADD, &add_button_);
    lv_obj_set_style_opa(add_button_, LV_OPA_40, LV_STATE_DISABLED);

    strip_ = CreatePlain(root_);
    lv_obj_set_size(strip_, 320, kStripHeight);
    lv_obj_set_pos(strip_, 0, kRodakosAppHeaderHeight);
    lv_obj_set_style_border_side(strip_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(strip_, rodakos_theme_divider(), 0);
    lv_obj_set_style_border_width(strip_, 1, 0);
    strip_dot_ = CreatePlain(strip_);
    lv_obj_set_size(strip_dot_, 7, 7);
    lv_obj_set_style_radius(strip_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(strip_dot_, LV_OPA_COVER, 0);
    lv_obj_align(strip_dot_, LV_ALIGN_LEFT_MID, 12, 0);
    strip_label_ = CreateText(strip_, "", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(strip_label_, 200);
    lv_label_set_long_mode(strip_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(strip_label_, LV_ALIGN_LEFT_MID, 25, 0);
    strip_right_ = lv_btn_create(strip_);
    lv_obj_remove_style_all(strip_right_);
    lv_obj_set_size(strip_right_, LV_SIZE_CONTENT, kStripHeight);
    lv_obj_set_style_pad_hor(strip_right_, 4, 0);
    lv_obj_set_flex_flow(strip_right_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip_right_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(strip_right_, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_ext_click_area(strip_right_, 8);
    CreateText(strip_right_, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_add_event_cb(strip_right_, [](lv_event_t* e) {
        auto* self = static_cast<WolApp*>(lv_event_get_user_data(e));
        if (!self->WifiConnected()) self->OpenWiFiSettings();
    }, LV_EVENT_CLICKED, this);

    device_list_ = CreatePlain(root_);
    lv_obj_set_size(device_list_, 320, 240 - kListTop);
    lv_obj_set_pos(device_list_, 0, kListTop);
    lv_obj_add_flag(device_list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(device_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(device_list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_bottom(device_list_, kCardGap, 0);

    status_box_ = CreatePlain(root_);
    lv_obj_set_size(status_box_, 320, 240 - kListTop);
    lv_obj_set_pos(status_box_, 0, kListTop);

    UpdateStrip();
    RebuildDeviceList();
}

void WolApp::DestroyUi() {
    soft_keyboard_.Hide();
    if (sent_timer_ != nullptr) {
        lv_timer_delete(sent_timer_);
        sent_timer_ = nullptr;
    }
    if (dialog_ != nullptr && lv_obj_is_valid(dialog_)) lv_obj_delete(dialog_);
    if (input_ != nullptr && lv_obj_is_valid(input_)) lv_obj_delete(input_);
    if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = title_label_ = add_button_ = strip_ = strip_dot_ = strip_label_ = strip_right_ = nullptr;
    device_list_ = status_box_ = nullptr;
    editor_ = editor_title_ = editor_hint_ = editor_delete_ = nullptr;
    for (auto*& value : editor_values_) value = nullptr;
    input_ = input_textarea_ = input_hint_ = dialog_ = nullptr;
    editing_index_ = kNoDevice;
    sent_index_ = kNoDevice;
}

bool WolApp::WifiConnected() const {
    WiFiAdapter* wifi = context_ != nullptr ? context_->services().wifi() : nullptr;
    return wifi != nullptr && wifi->GetStatus() == WiFiStatus::kConnected;
}

void WolApp::UpdateStrip() {
    if (strip_label_ == nullptr) return;
    auto* right = lv_obj_get_child(strip_right_, 0);
    const bool broken = load_status_ == rodakos::WolDeviceLoadStatus::kCorrupt ||
                        load_status_ == rodakos::WolDeviceLoadStatus::kTooLarge ||
                        load_status_ == rodakos::WolDeviceLoadStatus::kStorageError;
    const bool read_only = !storage_write_allowed_ && !storage_reset_available_;
    lv_color_t dot = rodakos_theme_success();
    if (WifiConnected()) {
        WiFiAdapter* wifi = context_->services().wifi();
        lv_label_set_text(strip_label_, wifi->GetConnectedSSID().c_str());
        lv_obj_set_style_text_color(strip_label_, rodakos_theme_text_secondary(), 0);
        if (read_only && !broken) {
            lv_label_set_text(right, "列表只读");
            lv_obj_set_style_text_color(right, rodakos_theme_warning(), 0);
        } else {
            lv_label_set_text_fmt(right, "%u / %u", static_cast<unsigned>(devices_.size()),
                                  static_cast<unsigned>(rodakos::kWolMaxDevices));
            lv_obj_set_style_text_color(right, rodakos_theme_text_tertiary(), 0);
        }
        if (lv_obj_get_child_count(strip_right_) > 1) lv_obj_delete(lv_obj_get_child(strip_right_, 1));
    } else {
        dot = rodakos_theme_warning();
        lv_label_set_text(strip_label_, "Wi-Fi 未连接");
        lv_obj_set_style_text_color(strip_label_, rodakos_theme_warning(), 0);
        lv_label_set_text(right, "去设置");
        lv_obj_set_style_text_color(right, rodakos_theme_primary(), 0);
        if (lv_obj_get_child_count(strip_right_) < 2) {
            CreateText(strip_right_, ICON_CHEVRON_RIGHT, PhoneIconFont(), rodakos_theme_primary());
        }
    }
    lv_obj_set_style_bg_color(strip_dot_, dot, 0);
    // The right-hand item is content-sized; re-anchor it after its text changes width.
    lv_obj_update_layout(strip_right_);
    lv_obj_align(strip_right_, LV_ALIGN_RIGHT_MID, -8, 0);
    UpdateHeaderAction();
}

void WolApp::UpdateHeaderAction() {
    if (add_button_ == nullptr) return;
    const bool can_add = storage_write_allowed_ && devices_.size() < rodakos::kWolMaxDevices;
    if (can_add) {
        lv_obj_remove_state(add_button_, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(add_button_, LV_STATE_DISABLED);
    }
}

void WolApp::RebuildDeviceList() {
    if (device_list_ == nullptr) return;
    lv_obj_clean(device_list_);
    lv_obj_clean(status_box_);

    // A damaged list says so and offers a confirmed reset; it never pretends to be empty.
    if (storage_reset_available_) {
        SetHidden(device_list_, true);
        SetHidden(status_box_, false);
        auto* icon = CreateText(status_box_, ICON_STORAGE, PhoneIconFontLarge(), rodakos_theme_warning());
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 16);
        auto* title = CreateText(status_box_, "设备列表数据损坏", &phone_font_14, rodakos_theme_text_primary());
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 54);
        auto* hint = CreateText(status_box_, "重置列表后可重新添加设备", &phone_font_12, rodakos_theme_text_secondary());
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 78);
        auto* reset = CreateFilledButton(status_box_, ICON_REFRESH, "重置列表", 130, 38, rodakos_theme_primary(),
                                         rodakos_theme_on_primary());
        lv_obj_align(reset, LV_ALIGN_TOP_MID, 0, 110);
        lv_obj_add_event_cb(reset, [](lv_event_t* e) {
            static_cast<WolApp*>(lv_event_get_user_data(e))->AskReset();
        }, LV_EVENT_CLICKED, this);
        return;
    }
    if (devices_.empty()) {
        SetHidden(device_list_, true);
        SetHidden(status_box_, false);
        auto* icon = CreateText(status_box_, ICON_POWER, PhoneIconFontLarge(), rodakos_theme_text_tertiary());
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 16);
        auto* title = CreateText(status_box_, "还没有设备", &phone_font_14, rodakos_theme_text_primary());
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 54);
        auto* hint = CreateText(status_box_, "添加电脑或 NAS 的 MAC 地址即可开机", &phone_font_12,
                                rodakos_theme_text_secondary());
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 78);
        if (storage_write_allowed_) {
            auto* add = CreateFilledButton(status_box_, ICON_ADD, "添加设备", 130, 38, rodakos_theme_primary(),
                                           rodakos_theme_on_primary());
            lv_obj_align(add, LV_ALIGN_TOP_MID, 0, 110);
            lv_obj_add_event_cb(add, [](lv_event_t* e) {
                static_cast<WolApp*>(lv_event_get_user_data(e))->ShowEditor(kNewDeviceIndex);
            }, LV_EVENT_CLICKED, this);
        }
        return;
    }
    SetHidden(device_list_, false);
    SetHidden(status_box_, true);
    for (size_t i = 0; i < devices_.size(); ++i) {
        CreateDeviceCard(i, static_cast<lv_coord_t>(kCardGap + i * (kCardHeight + kCardGap)));
    }
}

void WolApp::CreateDeviceCard(size_t index, lv_coord_t y) {
    const rodakos::WolDevice& device = devices_[index];
    const bool connected = WifiConnected();
    const bool sent = index == sent_index_;

    auto* card = CreatePlain(device_list_);
    lv_obj_set_size(card, 300, kCardHeight);
    lv_obj_set_pos(card, 10, y);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_clip_corner(card, true, 0);

    // Left 242 px opens the editor; the power button on the right is a separate target.
    auto* info = lv_btn_create(card);
    lv_obj_remove_style_all(info);
    lv_obj_set_size(info, 242, kCardHeight);
    lv_obj_set_style_bg_color(info, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(info, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(info, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_user_data(info, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
    lv_obj_add_event_cb(info, [](lv_event_t* e) {
        auto* self = static_cast<WolApp*>(lv_event_get_user_data(e));
        auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        self->ShowEditor(static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target))));
    }, LV_EVENT_CLICKED, this);
    auto* name = CreateText(info, device.name.c_str(), &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_size(name, 218, 20);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(name, 12, 6);
    std::string sub = sent ? "已发送唤醒包 · 端口 " + std::to_string(device.port)
                           : device.mac_address + " · 端口 " + std::to_string(device.port);
    auto* detail = CreateText(info, sub.c_str(), &phone_font_12, sent ? rodakos_theme_success() : rodakos_theme_text_tertiary());
    lv_obj_set_size(detail, 218, 16);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(detail, 12, 30);

    auto* divider = CreatePlain(card);
    lv_obj_set_size(divider, 1, 32);
    lv_obj_set_pos(divider, 242, 10);
    lv_obj_set_style_bg_color(divider, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    const lv_color_t bg = sent ? rodakos_theme_success() : connected ? rodakos_theme_primary() : rodakos_theme_bg_tertiary();
    const lv_color_t fg = sent ? rodakos_theme_text_on(rodakos_theme_get()->success)
                        : connected ? rodakos_theme_on_primary() : rodakos_theme_text_tertiary();
    auto* power = CreateFilledButton(card, sent ? ICON_CHECK : ICON_POWER, nullptr, 46, 40, bg, fg);
    lv_obj_align(power, LV_ALIGN_RIGHT_MID, -6, 0);
    if (!connected && !sent) lv_obj_set_style_opa(power, LV_OPA_50, 0);
    lv_obj_set_user_data(power, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
    lv_obj_add_event_cb(power, [](lv_event_t* e) {
        auto* self = static_cast<WolApp*>(lv_event_get_user_data(e));
        auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        self->WakeDevice(static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target))));
    }, LV_EVENT_CLICKED, this);
}

void WolApp::WakeDevice(size_t index) {
    if (index >= devices_.size()) return;
    if (!WifiConnected()) {
        ui_->ShowToastUnlocked("请先连接 Wi-Fi");
        return;
    }
    if (wake_on_lan_ == nullptr) {
        ui_->ShowToastUnlocked("唤醒服务不可用");
        return;
    }
    const rodakos::WolDevice& device = devices_[index];
    const rodakos::WakeOnLanResult result = wake_on_lan_->Send(device.mac_address, device.broadcast_address, device.port);
    if (result != rodakos::WakeOnLanResult::kSent) {
        ui_->ShowToastUnlocked(SendResultMessage(result));
        UpdateStrip();
        return;
    }
    // A green check on the card for 2 s confirms the send without a toast covering the list.
    sent_index_ = index;
    if (sent_timer_ != nullptr) lv_timer_delete(sent_timer_);
    sent_timer_ = lv_timer_create([](lv_timer_t* timer) {
        static_cast<WolApp*>(lv_timer_get_user_data(timer))->ClearSentFeedback();
    }, kSentFeedbackMs, this);
    lv_timer_set_repeat_count(sent_timer_, 1);
    RebuildDeviceList();
}

void WolApp::ClearSentFeedback() {
    // A one-shot timer deletes itself after this callback.
    sent_timer_ = nullptr;
    sent_index_ = kNoDevice;
    RebuildDeviceList();
}

void WolApp::ShowEditor(size_t index) {
    if (editor_ != nullptr || (index != kNewDeviceIndex && index >= devices_.size())) return;
    if (!storage_write_allowed_) {
        ui_->ShowToastUnlocked("设备列表只读");
        return;
    }
    editing_index_ = index;
    draft_ = index == kNewDeviceIndex ? rodakos::WolDevice{} : devices_[index];

    editor_ = CreatePlain(root_);
    lv_obj_set_size(editor_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(editor_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(editor_, LV_OPA_COVER, 0);
    lv_obj_add_flag(editor_, LV_OBJ_FLAG_CLICKABLE);

    auto* header = CreateAppHeader(editor_, index == kNewDeviceIndex ? "添加设备" : "编辑设备", [](lv_event_t* e) {
        static_cast<WolApp*>(lv_event_get_user_data(e))->CloseEditor();
    }, [](lv_event_t* e) {
        static_cast<WolApp*>(lv_event_get_user_data(e))->SaveDraft();
    }, this, &editor_title_, ICON_CHECK);
    auto* save = lv_obj_get_child(header, -1);
    lv_obj_set_style_bg_color(save, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(save, 0), rodakos_theme_on_primary(), 0);

    auto* form = CreatePlain(editor_);
    lv_obj_set_size(form, 300, kFormRowHeight * 4);
    lv_obj_set_pos(form, 10, 48);
    lv_obj_set_style_bg_color(form, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(form, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(form, 8, 0);
    lv_obj_set_style_clip_corner(form, true, 0);
    for (int i = 0; i < 4; ++i) {
        auto* row = lv_btn_create(form);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 300, kFormRowHeight);
        lv_obj_set_pos(row, 0, i * kFormRowHeight);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        if (i > 0) {
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
            lv_obj_set_style_border_color(row, rodakos_theme_bg_tertiary(), 0);
            lv_obj_set_style_border_width(row, 1, 0);
        }
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        auto* key = CreateText(row, FieldTitle(i), &phone_font_14, rodakos_theme_text_secondary());
        lv_obj_align(key, LV_ALIGN_LEFT_MID, 12, 0);
        editor_values_[i] = CreateText(row, "", &phone_font_14, rodakos_theme_text_primary());
        lv_obj_set_width(editor_values_[i], 176);
        lv_label_set_long_mode(editor_values_[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(editor_values_[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(editor_values_[i], LV_ALIGN_RIGHT_MID, -30, 0);
        auto* chevron = CreateText(row, ICON_CHEVRON_RIGHT, PhoneIconFont(), rodakos_theme_text_tertiary());
        lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            auto* self = static_cast<WolApp*>(lv_event_get_user_data(e));
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            self->ShowInput(static_cast<Field>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target))));
        }, LV_EVENT_CLICKED, this);
    }

    if (index == kNewDeviceIndex) {
        editor_hint_ = CreateText(editor_, "广播地址和端口已预填常用值", &phone_font_12,
                                  rodakos_theme_text_tertiary());
        lv_obj_set_width(editor_hint_, 292);
        lv_obj_set_style_text_align(editor_hint_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(editor_hint_, 14, 202);
    } else {
        editor_delete_ = lv_btn_create(editor_);
        lv_obj_remove_style_all(editor_delete_);
        lv_obj_set_size(editor_delete_, 300, 38);
        lv_obj_set_pos(editor_delete_, 10, 194);
        lv_obj_set_style_radius(editor_delete_, 8, 0);
        lv_obj_set_style_border_color(editor_delete_, rodakos_theme_error(), 0);
        lv_obj_set_style_border_width(editor_delete_, 1, 0);
        lv_obj_set_style_bg_color(editor_delete_, rodakos_theme_error(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(editor_delete_, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_set_flex_flow(editor_delete_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(editor_delete_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(editor_delete_, 6, 0);
        CreateText(editor_delete_, ICON_TRASH, PhoneIconFont(), rodakos_theme_error());
        CreateText(editor_delete_, "删除设备", &phone_font_14, rodakos_theme_error());
        lv_obj_add_event_cb(editor_delete_, [](lv_event_t* e) {
            static_cast<WolApp*>(lv_event_get_user_data(e))->AskDelete();
        }, LV_EVENT_CLICKED, this);
    }
    RefreshEditor();
}

void WolApp::RefreshEditor() {
    if (editor_ == nullptr) return;
    const std::string port = std::to_string(draft_.port);
    const char* values[] = {draft_.name.c_str(), draft_.mac_address.c_str(), draft_.broadcast_address.c_str(),
                            port.c_str()};
    const char* placeholders[] = {"未填写", "未填写", "255.255.255.255", "9"};
    for (int i = 0; i < 4; ++i) {
        const bool empty = values[i][0] == '\0' || (i == 3 && draft_.port == 0);
        lv_label_set_text(editor_values_[i], empty ? placeholders[i] : values[i]);
        lv_obj_set_style_text_color(editor_values_[i], empty ? rodakos_theme_text_tertiary() : rodakos_theme_text_primary(), 0);
    }
}

void WolApp::CloseEditor() {
    CloseInput();
    if (editor_ != nullptr && lv_obj_is_valid(editor_)) lv_obj_delete_async(editor_);
    editor_ = editor_title_ = editor_hint_ = editor_delete_ = nullptr;
    for (auto*& value : editor_values_) value = nullptr;
    editing_index_ = kNoDevice;
}

void WolApp::ShowInput(Field field) {
    if (editor_ == nullptr || input_ != nullptr) return;
    input_field_ = field;
    const int f = static_cast<int>(field);

    // Full-screen layer like the Wi-Fi password page: a 304x108 card above the 120 px keyboard.
    input_ = CreatePlain(lv_screen_active());
    lv_obj_set_size(input_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(input_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(input_, LV_OPA_COVER, 0);
    lv_obj_add_flag(input_, LV_OBJ_FLAG_CLICKABLE);

    auto* box = CreatePlain(input_);
    lv_obj_set_size(box, 304, 108);
    lv_obj_set_pos(box, 8, 8);
    lv_obj_set_style_bg_color(box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 8, 0);

    auto* caption = CreateText(box, FieldTitle(f), &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_pos(caption, 12, 8);

    // Port and broadcast address have one-tap presets next to the caption.
    if (field == Field::kPort || field == Field::kBroadcast) {
        auto* presets = CreatePlain(box);
        lv_obj_set_size(presets, LV_SIZE_CONTENT, 24);
        lv_obj_set_flex_flow(presets, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(presets, 6, 0);
        lv_obj_set_pos(presets, 96, 6);
        const char* values[2] = {"9", "7"};
        const char* labels[2] = {"9", "7"};
        int count = 2;
        if (field == Field::kBroadcast) {
            values[0] = "255.255.255.255";
            labels[0] = "默认 255";
            count = 1;
        }
        for (int i = 0; i < count; ++i) {
            auto* chip = lv_btn_create(presets);
            lv_obj_remove_style_all(chip);
            lv_obj_set_size(chip, LV_SIZE_CONTENT, 24);
            lv_obj_set_style_pad_hor(chip, 9, 0);
            lv_obj_set_style_radius(chip, 5, 0);
            lv_obj_set_style_bg_color(chip, rodakos_theme_bg_tertiary(), 0);
            lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
            lv_obj_set_ext_click_area(chip, 6);
            auto* label = CreateText(chip, labels[i], &phone_font_12, rodakos_theme_text_secondary());
            lv_obj_center(label);
            lv_obj_set_user_data(chip, const_cast<char*>(values[i]));
            lv_obj_add_event_cb(chip, [](lv_event_t* e) {
                auto* self = static_cast<WolApp*>(lv_event_get_user_data(e));
                auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                if (self->input_textarea_ != nullptr) {
                    lv_textarea_set_text(self->input_textarea_, static_cast<const char*>(lv_obj_get_user_data(target)));
                }
            }, LV_EVENT_CLICKED, this);
        }
    }

    input_hint_ = CreateText(box, FieldHint(f), &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(input_hint_, 244);
    lv_label_set_long_mode(input_hint_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(input_hint_, 12, 34);

    auto* close = lv_btn_create(box);
    lv_obj_remove_style_all(close);
    lv_obj_set_size(close, 34, 34);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_set_style_radius(close, 6, 0);
    lv_obj_set_style_bg_color(close, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, 0);
    auto* close_icon = CreateText(close, ICON_CLOSE, PhoneIconFont(), rodakos_theme_text_primary());
    lv_obj_center(close_icon);
    lv_obj_add_event_cb(close, [](lv_event_t* e) {
        static_cast<WolApp*>(lv_event_get_user_data(e))->CloseInput();
    }, LV_EVENT_CLICKED, this);

    input_textarea_ = lv_textarea_create(box);
    lv_obj_set_size(input_textarea_, 212, 36);
    lv_obj_set_pos(input_textarea_, 12, 54);
    lv_textarea_set_one_line(input_textarea_, true);
    lv_obj_set_style_bg_color(input_textarea_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(input_textarea_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(input_textarea_, &phone_font_14, 0);
    lv_obj_set_style_border_color(input_textarea_, rodakos_theme_primary(), 0);
    lv_obj_set_style_border_width(input_textarea_, 1, 0);
    lv_obj_set_style_radius(input_textarea_, 6, 0);
    std::string value;
    switch (field) {
        case Field::kName:
            lv_textarea_set_max_length(input_textarea_, rodakos::kWolDeviceNameMaxBytes);
            lv_textarea_set_placeholder_text(input_textarea_, "书房台式机");
            value = draft_.name;
            break;
        case Field::kMac:
            lv_textarea_set_max_length(input_textarea_, 17);
            lv_textarea_set_accepted_chars(input_textarea_, "0123456789abcdefABCDEF:-");
            lv_textarea_set_placeholder_text(input_textarea_, "AA:BB:CC:DD:EE:FF");
            value = draft_.mac_address;
            break;
        case Field::kBroadcast:
            lv_textarea_set_max_length(input_textarea_, 15);
            lv_textarea_set_accepted_chars(input_textarea_, "0123456789.");
            lv_textarea_set_placeholder_text(input_textarea_, "255.255.255.255");
            value = draft_.broadcast_address;
            break;
        case Field::kPort:
            lv_textarea_set_max_length(input_textarea_, 5);
            lv_textarea_set_accepted_chars(input_textarea_, "0123456789");
            lv_textarea_set_placeholder_text(input_textarea_, "9");
            value = draft_.port > 0 ? std::to_string(draft_.port) : "";
            break;
    }
    lv_textarea_set_text(input_textarea_, value.c_str());

    // 54..90 keeps the OK button clear of the keyboard's 收起 tab (y 96..118 on screen).
    auto* ok = CreateFilledButton(box, nullptr, "确定", 64, 36, rodakos_theme_primary(), rodakos_theme_on_primary());
    lv_obj_align(ok, LV_ALIGN_TOP_RIGHT, -10, 54);
    lv_obj_add_event_cb(ok, [](lv_event_t* e) {
        static_cast<WolApp*>(lv_event_get_user_data(e))->SubmitInput();
    }, LV_EVENT_CLICKED, this);

    soft_keyboard_.Show(input_textarea_, [this]() {
        // Runs from the keyboard event; defer so the keyboard is not torn down inside it.
        lv_async_call([](void* user_data) { static_cast<WolApp*>(user_data)->SubmitInput(); }, this);
    });
}

void WolApp::CloseInput() {
    soft_keyboard_.Hide();
    if (input_ != nullptr && lv_obj_is_valid(input_)) lv_obj_delete_async(input_);
    input_ = input_textarea_ = input_hint_ = nullptr;
}

void WolApp::SubmitInput() {
    if (input_textarea_ == nullptr) return;
    const std::string text = Trim(lv_textarea_get_text(input_textarea_));
    // Validate the one field now so a mistake is shown where it was typed.
    rodakos::WolDevice probe = draft_;
    rodakos::WolDeviceValidationStatus status = rodakos::WolDeviceValidationStatus::kOk;
    switch (input_field_) {
        case Field::kName:
            probe.name = text;
            if (text.empty()) status = rodakos::WolDeviceValidationStatus::kInvalidName;
            break;
        case Field::kMac: {
            probe.mac_address = text;
            if (probe.name.empty()) probe.name = "x";
            status = rodakos::NormalizeWolDevice(&probe);
            if (status != rodakos::WolDeviceValidationStatus::kInvalidMac) status = rodakos::WolDeviceValidationStatus::kOk;
            break;
        }
        case Field::kBroadcast: {
            probe.broadcast_address = text;
            if (probe.name.empty()) probe.name = "x";
            if (probe.mac_address.empty()) probe.mac_address = "02:00:00:00:00:01";
            status = rodakos::NormalizeWolDevice(&probe);
            if (status != rodakos::WolDeviceValidationStatus::kInvalidBroadcastAddress) {
                status = rodakos::WolDeviceValidationStatus::kOk;
            }
            break;
        }
        case Field::kPort: {
            char* end = nullptr;
            const unsigned long value = std::strtoul(text.c_str(), &end, 10);
            const bool valid = !text.empty() && end != text.c_str() && *end == '\0' && value >= 1 && value <= 65535;
            probe.port = valid ? static_cast<uint16_t>(value) : 0;
            if (!valid) status = rodakos::WolDeviceValidationStatus::kInvalidPort;
            break;
        }
    }
    if (status != rodakos::WolDeviceValidationStatus::kOk) {
        lv_label_set_text(input_hint_, ValidationMessage(status));
        lv_obj_set_style_text_color(input_hint_, rodakos_theme_error(), 0);
        return;
    }
    switch (input_field_) {
        case Field::kName: draft_.name = probe.name; break;
        case Field::kMac: draft_.mac_address = probe.mac_address; break;
        case Field::kBroadcast: draft_.broadcast_address = probe.broadcast_address; break;
        case Field::kPort: draft_.port = probe.port; break;
    }
    CloseInput();
    RefreshEditor();
}

void WolApp::SaveDraft() {
    if (!storage_write_allowed_) {
        ui_->ShowToastUnlocked("设备列表只读");
        return;
    }
    rodakos::WolDevice device = draft_;
    const auto validation = rodakos::NormalizeWolDevice(&device);
    if (validation != rodakos::WolDeviceValidationStatus::kOk) {
        ui_->ShowToastUnlocked(ValidationMessage(validation));
        return;
    }
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (i != editing_index_ && devices_[i].mac_address == device.mac_address) {
            ui_->ShowToastUnlocked("这个 MAC 地址已经添加过");
            return;
        }
    }
    std::vector<rodakos::WolDevice> candidate = devices_;
    if (editing_index_ == kNewDeviceIndex) {
        if (candidate.size() >= rodakos::kWolMaxDevices) {
            ui_->ShowToastUnlocked("最多保存 8 台设备");
            return;
        }
        candidate.push_back(device);
    } else if (editing_index_ < candidate.size()) {
        candidate[editing_index_] = device;
    } else {
        return;
    }
    if (!store_.Save(candidate)) {
        storage_write_allowed_ = store_.write_allowed();
        ui_->ShowToastUnlocked("保存失败");
        UpdateStrip();
        return;
    }
    devices_ = std::move(candidate);
    CloseEditor();
    RebuildDeviceList();
    UpdateStrip();
    ui_->ShowToastUnlocked("已保存");
}

void WolApp::AskDelete() {
    if (editing_index_ >= devices_.size() || dialog_ != nullptr) return;
    dialog_is_reset_ = false;
    const auto& device = devices_[editing_index_];
    dialog_ = CreatePlain(lv_layer_top());
    lv_obj_set_size(dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(dialog_, rodakos_theme_overlay(), 0);
    lv_obj_set_style_bg_opa(dialog_, LV_OPA_70, 0);
    lv_obj_add_flag(dialog_, LV_OBJ_FLAG_CLICKABLE);
    auto* box = CreatePlain(dialog_);
    lv_obj_set_size(box, 296, 136);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_border_color(box, rodakos_theme_border(), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    auto* title = CreateText(box, "删除这台设备？", &phone_font_18, rodakos_theme_text_primary());
    lv_obj_set_pos(title, 14, 12);
    const std::string text = device.name + "（" + device.mac_address + "）将从列表移除。";
    auto* message = CreateText(box, text.c_str(), &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_size(message, 266, 36);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(message, 14, 42);
    auto* cancel = CreateFilledButton(box, nullptr, "取消", 128, 38, rodakos_theme_bg_tertiary(), rodakos_theme_text_primary());
    lv_obj_set_pos(cancel, 14, 86);
    lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
        static_cast<WolApp*>(lv_event_get_user_data(e))->CloseDialog();
    }, LV_EVENT_CLICKED, this);
    auto* confirm = CreateFilledButton(box, nullptr, "删除", 128, 38, rodakos_theme_error(), rodakos_theme_on_error());
    lv_obj_set_pos(confirm, 154, 86);
    lv_obj_add_event_cb(confirm, [](lv_event_t* e) {
        static_cast<WolApp*>(lv_event_get_user_data(e))->ConfirmDialog();
    }, LV_EVENT_CLICKED, this);
}

void WolApp::AskReset() {
    if (dialog_ != nullptr) return;
    dialog_is_reset_ = true;
    dialog_ = CreatePlain(lv_layer_top());
    lv_obj_set_size(dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(dialog_, rodakos_theme_overlay(), 0);
    lv_obj_set_style_bg_opa(dialog_, LV_OPA_70, 0);
    lv_obj_add_flag(dialog_, LV_OBJ_FLAG_CLICKABLE);
    auto* box = CreatePlain(dialog_);
    lv_obj_set_size(box, 296, 136);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_border_color(box, rodakos_theme_border(), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    auto* title = CreateText(box, "重置设备列表？", &phone_font_18, rodakos_theme_text_primary());
    lv_obj_set_pos(title, 14, 12);
    auto* message = CreateText(box, "损坏的列表会被清空，之后需要重新添加设备。", &phone_font_12,
                               rodakos_theme_text_secondary());
    lv_obj_set_size(message, 266, 36);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(message, 14, 42);
    auto* cancel = CreateFilledButton(box, nullptr, "取消", 128, 38, rodakos_theme_bg_tertiary(), rodakos_theme_text_primary());
    lv_obj_set_pos(cancel, 14, 86);
    lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
        static_cast<WolApp*>(lv_event_get_user_data(e))->CloseDialog();
    }, LV_EVENT_CLICKED, this);
    auto* confirm = CreateFilledButton(box, nullptr, "重置", 128, 38, rodakos_theme_error(), rodakos_theme_on_error());
    lv_obj_set_pos(confirm, 154, 86);
    lv_obj_add_event_cb(confirm, [](lv_event_t* e) {
        static_cast<WolApp*>(lv_event_get_user_data(e))->ConfirmDialog();
    }, LV_EVENT_CLICKED, this);
}

void WolApp::CloseDialog() {
    // Deleting from inside one of its own button callbacks is not safe; defer it.
    if (dialog_ != nullptr && lv_obj_is_valid(dialog_)) lv_obj_delete_async(dialog_);
    dialog_ = nullptr;
}

void WolApp::ConfirmDialog() {
    const bool reset = dialog_is_reset_;
    CloseDialog();
    if (reset) {
        if (!store_.Reset()) {
            storage_reset_available_ = store_.reset_allowed();
            ui_->ShowToastUnlocked("重置失败");
            UpdateStrip();
            return;
        }
        devices_.clear();
        load_status_ = rodakos::WolDeviceLoadStatus::kMissing;
        storage_write_allowed_ = true;
        storage_reset_available_ = false;
        RebuildDeviceList();
        UpdateStrip();
        ui_->ShowToastUnlocked("设备列表已重置");
        return;
    }
    if (editing_index_ >= devices_.size() || !storage_write_allowed_) return;
    std::vector<rodakos::WolDevice> candidate = devices_;
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(editing_index_));
    if (!store_.Save(candidate)) {
        storage_write_allowed_ = store_.write_allowed();
        ui_->ShowToastUnlocked("删除失败");
        UpdateStrip();
        return;
    }
    devices_ = std::move(candidate);
    sent_index_ = kNoDevice;
    CloseEditor();
    RebuildDeviceList();
    UpdateStrip();
    ui_->ShowToastUnlocked("已删除");
}

void WolApp::OpenWiFiSettings() {
    if (context_ == nullptr) return;
    lv_async_call_cancel(DeferOpenWiFi, context_);
    lv_async_call(DeferOpenWiFi, context_);
}

void WolApp::NavigateHome() {
    if (context_ == nullptr) return;
    lv_async_call_cancel(DeferReturnHome, context_);
    lv_async_call(DeferReturnHome, context_);
}

void RegisterWolApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "wol",
        .title = "Wake",
        .icon = ICON_POWER,
        .category = PhoneAppCategory::kTools,
        .icon_tint = PhoneAppIconTint::kAlert,
        .capabilities = PhoneCapability::kNetwork,
        .show_on_home = true,
        .aliases = {"wake", "wake on lan", "network wake", "网络唤醒", "唤醒"},
        .create = []() { return std::make_unique<WolApp>(); },
    });
}
