#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"
#include "apps/settings/device_cloud_page_model.h"

#include "phone_os/device_cloud_config.h"
#include "phone_os/device_pairing_policy.h"
#include "phone_os/lan_onboarding_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_os/setup_qr_scan_service.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <string>

namespace {
const char* CloudDiagnosticTitleZh(rodakos::CloudDiagnosticCode code) {
    using Code = rodakos::CloudDiagnosticCode;
    switch (code) {
        case Code::kReady: return "已连接";
        case Code::kUnconfigured: return "尚未连接";
        case Code::kRefreshing: return "正在更新凭据";
        case Code::kCredentialsRejected: return "设备凭据被拒绝";
        case Code::kCredentialsExpired: return "设备凭据需要更新";
        case Code::kRefreshFailed: return "凭据更新失败";
        case Code::kNetworkUnavailable: return "无法连接服务器";
        case Code::kVoiceUnavailable: return "语音服务不可用";
        case Code::kTrustUnavailable: return "服务器证书需要处理";
        case Code::kCancelled: return "连接已取消";
        case Code::kDeviceMemoryLow: return "设备内存不足";
    }
    return "连接需要处理";
}

const char* CloudDiagnosticHintZh(rodakos::CloudDiagnosticCode code) {
    using Code = rodakos::CloudDiagnosticCode;
    switch (code) {
        case Code::kCredentialsRejected: return "请在服务端确认设备权限，然后点「重试」。";
        case Code::kCredentialsExpired:
        case Code::kRefreshFailed: return "请确认服务端在运行，然后点「重试」。";
        case Code::kNetworkUnavailable: return "请检查 Wi-Fi 和服务器地址，然后点「重试」。";
        case Code::kVoiceUnavailable: return "请在服务端启用语音，然后点「重试」。";
        case Code::kTrustUnavailable: return "服务器证书与固定的不一致。绑定会保留，请检查 USB 下发的服务器信任。";
        case Code::kDeviceMemoryLow: return "请关闭其他应用或重启设备后重试。";
        default: break;
    }
    return "请点「重试」。";
}

bool IsExpectedPairingWait(const std::string& error) {
    return error.empty() || error.rfind("等待设备绑定确认", 0) == 0;
}

struct CloudRefreshPayload {
    std::shared_ptr<SettingsCloudRefreshGuard> guard;
    rodakos::DeviceCloudConfigService* service = nullptr;
    uint32_t generation = 0;
    bool ok = false;
    rodakos::DeviceCloudConfig config;
    std::string error;
};

struct CloudUnbindPayload {
    std::shared_ptr<SettingsCloudRefreshGuard> guard;
    rodakos::DeviceCloudConfigService* service = nullptr;
    uint32_t generation = 0;
    bool ok = false;
    rodakos::DeviceCloudConfig config;
    std::string error;
};

void CloudRefreshCompleteCallback(void* user_data) {
    auto* payload = static_cast<CloudRefreshPayload*>(user_data);
    if (payload == nullptr) {
        return;
    }
    auto guard = payload->guard;
    SettingsApp* app = guard ? guard->app.load() : nullptr;
    if (app != nullptr) {
        app->OnDeviceCloudRefreshComplete(payload->ok,
                                          payload->config,
                                          payload->error,
                                          payload->generation);
    }
    delete payload;
}

void CloudRefreshTask(void* arg) {
    auto* payload = static_cast<CloudRefreshPayload*>(arg);
    if (payload != nullptr && payload->service != nullptr) {
        payload->ok = payload->service->Refresh(payload->config);
        if (!payload->ok) {
            payload->error = payload->service->last_error();
        }
        bool queued = false;
        if (lvgl_port_lock(1000)) {
            queued = lv_async_call(CloudRefreshCompleteCallback, payload) == LV_RESULT_OK;
            lvgl_port_unlock();
        }
        if (!queued) {
            auto guard = payload->guard;
            if (guard && payload->generation == guard->refresh_generation.load()) {
                guard->refresh_failure.store(SettingsCloudRefreshFailure::kCompletionDelivery);
                guard->refresh_in_progress.store(false);
            }
            delete payload;
        }
    } else {
        delete payload;
    }
    vTaskDelete(nullptr);
}

void CloudUnbindCompleteCallback(void* user_data) {
    auto* payload = static_cast<CloudUnbindPayload*>(user_data);
    auto guard = payload != nullptr ? payload->guard : nullptr;
    SettingsApp* app = guard ? guard->app.load() : nullptr;
    if (app != nullptr) {
        app->OnDeviceCloudUnbindComplete(payload->ok, payload->error,
                                         payload->generation);
    }
    delete payload;
}

void CloudUnbindTask(void* arg) {
    auto* payload = static_cast<CloudUnbindPayload*>(arg);
    if (payload != nullptr && payload->service != nullptr) {
        payload->ok = payload->service->Unbind(payload->config);
        payload->error = payload->ok ? std::string() : payload->service->last_error();
        bool queued = false;
        if (lvgl_port_lock(1000)) {
            queued = lv_async_call(CloudUnbindCompleteCallback, payload) == LV_RESULT_OK;
            lvgl_port_unlock();
        }
        if (!queued) {
            if (payload->guard) {
                payload->guard->refresh_in_progress.store(false);
            }
            delete payload;
        }
    } else {
        delete payload;
    }
    vTaskDelete(nullptr);
}
}  // namespace

using namespace rodakos_settings;

namespace {

// The display is 320x240 landscape; the page body below the 40 px header is 320x200 and does
// not scroll. The status card scrolls on its own if text grows; the action row is always visible.
constexpr lv_coord_t kGridX = 10;
constexpr lv_coord_t kGridWidth = 300;
constexpr lv_coord_t kCardY = 6;
constexpr lv_coord_t kCardHeight = 136;
constexpr lv_coord_t kActionY = 150;
constexpr lv_coord_t kActionHeight = 40;
constexpr lv_coord_t kActionGap = 8;
constexpr lv_coord_t kHalfWidth = (kGridWidth - kActionGap) / 2;

const char* ActionLabel(DeviceCloudAction action) {
    switch (action) {
        case DeviceCloudAction::kScanQr: return "扫码配置";
        case DeviceCloudAction::kLanOnboarding: return "局域网连接";
        case DeviceCloudAction::kManualPairing: return "按已有地址连接";
        case DeviceCloudAction::kCheckPairing: return "检查连接";
        case DeviceCloudAction::kRetry: return "重试";
        case DeviceCloudAction::kDetails: return "详情";
        case DeviceCloudAction::kUnbind: return "解除连接";
        case DeviceCloudAction::kContinueUnbind: return "继续解除";
        case DeviceCloudAction::kNone: break;
    }
    return "";
}

lv_obj_t* CreateCloudButton(lv_obj_t* parent, lv_obj_t** label_out) {
    auto* button = lv_btn_create(parent);
    lv_obj_set_style_radius(button, 7, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    auto* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    lv_obj_center(label);
    *label_out = label;
    return button;
}

void StyleCloudButton(lv_obj_t* button, lv_obj_t* label, DeviceCloudAction action, bool primary) {
    const bool danger = action == DeviceCloudAction::kUnbind;
    lv_obj_set_style_border_width(button, danger ? 1 : 0, 0);
    lv_obj_set_style_border_color(button, rodakos_theme_error(), 0);
    lv_obj_set_style_bg_opa(button, danger ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, primary ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(label,
                                danger ? rodakos_theme_error()
                                       : (primary ? lv_color_white() : rodakos_theme_text_primary()),
                                0);
    lv_label_set_text(label, ActionLabel(action));
}

lv_obj_t* CreateDetailRow(lv_obj_t* parent, const char* name, lv_obj_t** value_out) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    auto* key = CreateSettingLabel(row, name, true);
    lv_obj_set_style_text_font(key, &phone_font_12, 0);
    lv_obj_align(key, LV_ALIGN_TOP_LEFT, 0, 0);
    auto* value = CreateSettingLabel(row, "", false);
    lv_obj_set_width(value, 216);
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(value, &phone_font_12, 0);
    lv_obj_align(value, LV_ALIGN_TOP_LEFT, 64, 0);
    *value_out = value;
    return row;
}

std::string HostOf(const std::string& url) {
    const size_t scheme = url.find("://");
    const size_t start = scheme == std::string::npos ? 0 : scheme + 3;
    const size_t end = url.find('/', start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string GroupPairingCode(const std::string& code) {
    // Manual codes are 8 digits; keep leading zeros and split 4+4 for reading aloud.
    if (code.size() == 8) return code.substr(0, 4) + " " + code.substr(4);
    if (code.size() == 6) return code.substr(0, 3) + " " + code.substr(3);
    return code;
}

}  // namespace

void SettingsApp::CreateDeviceCloudPage() {
    device_cloud_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(device_cloud_body_);
    lv_obj_set_size(device_cloud_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(device_cloud_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(device_cloud_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(device_cloud_body_, 0, 0);
    lv_obj_clear_flag(device_cloud_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(device_cloud_body_, LV_OBJ_FLAG_HIDDEN);

    // ---- Main view: status card + fixed action row ----
    auto* card = lv_obj_create(device_cloud_body_);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, kGridWidth, kCardHeight);
    lv_obj_set_pos(card, kGridX, kCardY);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_hor(card, 12, 0);
    lv_obj_set_style_pad_ver(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_scroll_dir(card, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
    cloud_status_card_ = card;

    auto* heading = lv_obj_create(card);
    lv_obj_remove_style_all(heading);
    lv_obj_set_size(heading, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(heading, LV_OBJ_FLAG_SCROLLABLE);
    cloud_status_dot_ = lv_obj_create(heading);
    lv_obj_remove_style_all(cloud_status_dot_);
    lv_obj_set_size(cloud_status_dot_, 8, 8);
    lv_obj_set_style_radius(cloud_status_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(cloud_status_dot_, LV_OPA_COVER, 0);
    lv_obj_align(cloud_status_dot_, LV_ALIGN_LEFT_MID, 0, 0);
    cloud_status_label_ = CreateSettingLabel(heading, "", false);
    lv_obj_set_width(cloud_status_label_, 196);
    lv_label_set_long_mode(cloud_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(cloud_status_label_, LV_ALIGN_LEFT_MID, 14, 0);

    // A 48x32 touch target around a short text link; the visible text stays small.
    cloud_details_link_ = lv_btn_create(heading);
    lv_obj_remove_style_all(cloud_details_link_);
    lv_obj_set_size(cloud_details_link_, 56, 32);
    lv_obj_align(cloud_details_link_, LV_ALIGN_RIGHT_MID, 6, 0);
    auto* link_label = lv_label_create(cloud_details_link_);
    lv_label_set_text(link_label, "详情 >");
    lv_obj_set_style_text_color(link_label, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(link_label, &phone_font_12, 0);
    lv_obj_align(link_label, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_event_cb(cloud_details_link_, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowDeviceCloudDetails(true);
    }, LV_EVENT_CLICKED, this);

    cloud_guide_label_ = CreateSettingLabel(card, "", true);
    lv_obj_set_width(cloud_guide_label_, LV_PCT(100));
    lv_label_set_long_mode(cloud_guide_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(cloud_guide_label_, &phone_font_12, 0);

    // Large pairing code; hidden outside the pairing state.
    cloud_activation_label_ = lv_label_create(card);
    lv_obj_set_style_text_font(cloud_activation_label_, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(cloud_activation_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_letter_space(cloud_activation_label_, 3, 0);

    cloud_summary_label_ = CreateSettingLabel(card, "", false);
    lv_obj_set_width(cloud_summary_label_, LV_PCT(100));
    lv_label_set_long_mode(cloud_summary_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(cloud_summary_label_, &phone_font_12, 0);

    cloud_pairing_button_ = CreateCloudButton(device_cloud_body_, &cloud_pairing_button_label_);
    lv_obj_add_event_cb(cloud_pairing_button_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->RunDeviceCloudAction(self->cloud_primary_action_);
    }, LV_EVENT_CLICKED, this);
    cloud_secondary_button_ = CreateCloudButton(device_cloud_body_, &cloud_secondary_button_label_);
    lv_obj_add_event_cb(cloud_secondary_button_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->RunDeviceCloudAction(self->cloud_secondary_action_);
    }, LV_EVENT_CLICKED, this);

    // ---- Details view: same body, replaces the main view while open ----
    cloud_details_panel_ = lv_obj_create(device_cloud_body_);
    lv_obj_remove_style_all(cloud_details_panel_);
    lv_obj_set_size(cloud_details_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(cloud_details_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cloud_details_panel_, LV_OBJ_FLAG_HIDDEN);

    auto* details_card = lv_obj_create(cloud_details_panel_);
    lv_obj_remove_style_all(details_card);
    lv_obj_set_size(details_card, kGridWidth, kCardHeight);
    lv_obj_set_pos(details_card, kGridX, kCardY);
    lv_obj_set_style_bg_color(details_card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(details_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(details_card, 8, 0);
    lv_obj_set_style_pad_hor(details_card, 12, 0);
    lv_obj_set_style_pad_ver(details_card, 10, 0);
    lv_obj_set_flex_flow(details_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(details_card, 8, 0);
    lv_obj_set_scroll_dir(details_card, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(details_card, LV_SCROLLBAR_MODE_AUTO);
    CreateDetailRow(details_card, "服务地址", &cloud_url_label_);
    CreateDetailRow(details_card, "设备 ID", &cloud_client_id_label_);
    CreateDetailRow(details_card, "实时语音", &cloud_realtime_voice_label_);
    CreateDetailRow(details_card, "服务器信任", &cloud_trust_label_);

    // Details actions: edit URL and manual pairing on the left, unbind or back on the right.
    lv_obj_t* label = nullptr;
    cloud_edit_url_button_ = CreateCloudButton(cloud_details_panel_, &label);
    lv_obj_set_size(cloud_edit_url_button_, kHalfWidth, kActionHeight);
    lv_obj_set_pos(cloud_edit_url_button_, kGridX, kActionY);
    lv_obj_set_style_bg_color(cloud_edit_url_button_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(label, rodakos_theme_text_primary(), 0);
    lv_label_set_text(label, "编辑地址");
    lv_obj_add_event_cb(cloud_edit_url_button_, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowCloudProvisioningUrlDialog();
    }, LV_EVENT_CLICKED, this);

    cloud_manual_pair_button_ = CreateCloudButton(cloud_details_panel_, &label);
    lv_obj_set_size(cloud_manual_pair_button_, kHalfWidth, kActionHeight);
    lv_obj_set_pos(cloud_manual_pair_button_, kGridX + kHalfWidth + kActionGap, kActionY);
    lv_obj_set_style_bg_color(cloud_manual_pair_button_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(label, rodakos_theme_text_primary(), 0);
    lv_label_set_text(label, "按地址连接");
    lv_obj_add_event_cb(cloud_manual_pair_button_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowDeviceCloudDetails(false);
        self->RefreshDeviceCloud();
    }, LV_EVENT_CLICKED, this);

    cloud_unbind_button_ = CreateCloudButton(cloud_details_panel_, &label);
    lv_obj_set_size(cloud_unbind_button_, kHalfWidth, kActionHeight);
    lv_obj_set_pos(cloud_unbind_button_, kGridX + kHalfWidth + kActionGap, kActionY);
    StyleCloudButton(cloud_unbind_button_, label, DeviceCloudAction::kUnbind, false);
    lv_obj_add_event_cb(cloud_unbind_button_, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowDeviceCloudUnbindDialog();
    }, LV_EVENT_CLICKED, this);

    cloud_pairing_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
        if (self != nullptr && self->cloud_refresh_guard_ &&
            !self->cloud_refresh_guard_->refresh_in_progress.load()) {
            if (self->cloud_refresh_guard_->refresh_failure.load() != SettingsCloudRefreshFailure::kNone) {
                self->UpdateDeviceCloudPage();
                return;
            }
            self->RefreshDeviceCloud();
        }
    }, 2000, this);
    lv_timer_pause(cloud_pairing_timer_);
    UpdateDeviceCloudPage();
}

void SettingsApp::ShowDeviceCloudDetails(bool show) {
    if (cloud_details_panel_ == nullptr) return;
    cloud_details_open_ = show;
    if (header_title_label_ != nullptr) {
        lv_label_set_text(header_title_label_, show ? "连接详情" : "连接服务");
    }
    UpdateDeviceCloudPage();
}

void SettingsApp::RunDeviceCloudAction(DeviceCloudAction action) {
    switch (action) {
        case DeviceCloudAction::kScanQr: ShowSetupQr(); break;
        case DeviceCloudAction::kLanOnboarding: ShowLanOnboarding(); break;
        case DeviceCloudAction::kManualPairing:
        case DeviceCloudAction::kCheckPairing:
        case DeviceCloudAction::kRetry: RefreshDeviceCloud(); break;
        case DeviceCloudAction::kDetails: ShowDeviceCloudDetails(true); break;
        case DeviceCloudAction::kUnbind: ShowDeviceCloudUnbindDialog(); break;
        // Unbind() resumes the server-confirmed transaction; it never starts a new pairing.
        case DeviceCloudAction::kContinueUnbind: UnbindDeviceCloud(); break;
        case DeviceCloudAction::kNone: break;
    }
}

void SettingsApp::UpdateDeviceCloudPage() {
    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    if (device_cloud == nullptr || cloud_status_label_ == nullptr) {
        return;
    }

    rodakos::DeviceCloudConfig config;
    device_cloud->Load(config);
    const bool trust_setup_request =
        config.pairing_code == rodakos::kTrustSetupPairingCodePlaceholder;
    if (!config.pairing_code.empty() && !trust_setup_request) {
        cloud_pairing_code_ = config.pairing_code;
    }
    const std::string pairing_code = !config.pairing_code.empty() && !trust_setup_request
                                         ? config.pairing_code
                                         : cloud_pairing_code_;
    const auto local_failure = cloud_refresh_guard_ ? cloud_refresh_guard_->refresh_failure.load()
                                                   : SettingsCloudRefreshFailure::kNone;
    const auto diagnostic = device_cloud->diagnostic();
    const bool pairing_pending = config.has_pairing_request &&
        rodakos::ClassifyDevicePairingStatus(config.pairing_status) ==
            rodakos::DevicePairingStatus::kPending;

    DeviceCloudPageInputs in;
    in.aiot_registered = config.aiot_registered;
    in.aiot_activated = config.aiot_activated;
    in.unbind_pending = config.unbind_pending;
    in.server_trust_error = config.server_trust_error;
    in.pairing_pending = pairing_pending;
    in.pairing_error = (config.has_pairing_request && !pairing_pending) || !cloud_pairing_error_.empty();
    in.refresh_failed = local_failure != SettingsCloudRefreshFailure::kNone;
    in.refresh_in_progress = cloud_refresh_guard_ && cloud_refresh_guard_->refresh_in_progress.load();
    in.diagnostic_needs_attention = diagnostic != rodakos::CloudDiagnosticCode::kReady &&
                                    diagnostic != rodakos::CloudDiagnosticCode::kRefreshing &&
                                    diagnostic != rodakos::CloudDiagnosticCode::kUnconfigured;
    auto* scan = context_->services().setup_qr_scan();
    in.qr_available = scan != nullptr && scan->Available() && config.server_trust.empty() &&
                      !config.server_trust_pending;
    auto* onboarding = context_->services().lan_onboarding();
    in.lan_available = onboarding != nullptr && onboarding->Available();
    const DeviceCloudPageLayout layout = ResolveDeviceCloudPage(in);
    cloud_primary_action_ = layout.primary;
    cloud_secondary_action_ = layout.secondary;

    // ---- Status card text ----
    const char* title = "尚未连接";
    lv_color_t dot = rodakos_theme_text_tertiary();
    std::string guide;
    std::string summary;
    bool show_code = false;
    switch (layout.state) {
        case DeviceCloudPageState::kUnbound:
            title = "尚未连接";
            guide = layout.primary == DeviceCloudAction::kManualPairing
                ? "设备已有服务地址。点「按已有地址连接」，再在服务端输入屏幕上的配对码。"
                : "把设备接入 Rodak 或兼容服务，就能在电脑或手机上管理它。";
            if (layout.primary != DeviceCloudAction::kManualPairing) {
                summary = in.lan_available
                    ? "扫码：用控制台生成的配网二维码\n局域网：电脑上运行 Rodak"
                    : "扫码：用控制台生成的配网二维码";
            }
            break;
        case DeviceCloudPageState::kPairing:
            title = "等待在服务端确认";
            dot = rodakos_theme_warning();
            guide = "在服务端输入配对码，本页每 2 秒自动检查。";
            show_code = !pairing_code.empty();
            break;
        case DeviceCloudPageState::kBound:
            title = "已连接";
            dot = rodakos_theme_success();
            guide = "可在电脑上管理和控制设备。";
            summary = "服务器  " + HostOf(config.provisioning_url) + "\n语音      " +
                      (config.has_realtime_voice_config ? "已配置" : "未配置");
            break;
        case DeviceCloudPageState::kAttention:
            dot = rodakos_theme_error();
            if (in.refresh_failed) {
                title = local_failure == SettingsCloudRefreshFailure::kTaskStart ? "无法开始连接"
                                                                                : "未收到连接结果";
                guide = "请稍后点「重试」。";
            } else if (in.aiot_registered && in.aiot_activated) {
                title = CloudDiagnosticTitleZh(diagnostic);
                guide = CloudDiagnosticHintZh(diagnostic);
            } else {
                title = "配对未完成";
                guide = cloud_pairing_error_.empty() ? "配对码已失效或被拒绝，请点「重试」重新配对。"
                                                     : cloud_pairing_error_;
                show_code = !pairing_code.empty() && pairing_pending;
            }
            break;
        case DeviceCloudPageState::kUnbindPending:
            title = "解除连接未完成";
            dot = rodakos_theme_warning();
            guide = "服务器还没有确认解除连接。完成之前设备不会发起新的配对。";
            break;
        case DeviceCloudPageState::kTrustBroken:
            title = "服务器信任记录损坏";
            dot = rodakos_theme_error();
            guide = "无法读取已固定的服务器证书。请用 USB 重新下发服务器信任。";
            break;
    }
    if (in.refresh_in_progress && layout.state != DeviceCloudPageState::kBound) {
        title = "正在连接…";
    }
    lv_label_set_text(cloud_status_label_, title);
    lv_obj_set_style_bg_color(cloud_status_dot_, dot, 0);
    lv_label_set_text(cloud_guide_label_, guide.c_str());
    if (show_code) {
        lv_label_set_text(cloud_activation_label_, GroupPairingCode(pairing_code).c_str());
        lv_obj_clear_flag(cloud_activation_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(cloud_activation_label_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(cloud_summary_label_, summary.c_str());
    if (summary.empty()) lv_obj_add_flag(cloud_summary_label_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(cloud_summary_label_, LV_OBJ_FLAG_HIDDEN);
    if (layout.details_link) lv_obj_clear_flag(cloud_details_link_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(cloud_details_link_, LV_OBJ_FLAG_HIDDEN);

    // ---- Action row: one primary on the left; a lone action spans the full grid ----
    const bool two = layout.secondary != DeviceCloudAction::kNone;
    lv_obj_set_size(cloud_pairing_button_, two ? kHalfWidth : kGridWidth, kActionHeight);
    lv_obj_set_pos(cloud_pairing_button_, kGridX, kActionY);
    StyleCloudButton(cloud_pairing_button_, cloud_pairing_button_label_, layout.primary,
                     layout.primary != DeviceCloudAction::kDetails);
    if (layout.primary_busy &&
        layout.primary != DeviceCloudAction::kDetails) {
        lv_obj_add_state(cloud_pairing_button_, LV_STATE_DISABLED);
        lv_label_set_text(cloud_pairing_button_label_, "请稍候…");
    } else {
        lv_obj_clear_state(cloud_pairing_button_, LV_STATE_DISABLED);
    }
    if (two) {
        lv_obj_set_size(cloud_secondary_button_, kHalfWidth, kActionHeight);
        lv_obj_set_pos(cloud_secondary_button_, kGridX + kHalfWidth + kActionGap, kActionY);
        StyleCloudButton(cloud_secondary_button_, cloud_secondary_button_label_, layout.secondary, false);
        lv_obj_clear_flag(cloud_secondary_button_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(cloud_secondary_button_, LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Details view ----
    const bool bound = config.aiot_registered && config.aiot_activated && !config.unbind_pending;
    const bool pinned = !config.server_trust.empty() || config.server_trust_pending ||
                        config.server_trust_error;
    lv_label_set_text(cloud_url_label_,
                      config.provisioning_url.empty() ? "未设置" : config.provisioning_url.c_str());
    lv_label_set_text(cloud_client_id_label_, device_cloud->GetClientId().c_str());
    lv_label_set_text(cloud_realtime_voice_label_,
                      config.has_realtime_voice_config ? "已配置" : "未配置");
    lv_label_set_text(cloud_trust_label_,
                      config.server_trust_error ? "记录损坏，需 USB 修复"
                      : config.server_trust_pending ? "已下发，等待认证"
                      : !config.server_trust.empty() ? "已固定证书"
                      : "公共证书（未固定）");
    // A pinned endpoint can change only through USB or authenticated discovery.
    if (pinned) lv_obj_add_state(cloud_edit_url_button_, LV_STATE_DISABLED);
    else lv_obj_clear_state(cloud_edit_url_button_, LV_STATE_DISABLED);
    if (bound) {
        lv_obj_add_flag(cloud_manual_pair_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(cloud_unbind_button_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(cloud_manual_pair_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cloud_unbind_button_, LV_OBJ_FLAG_HIDDEN);
        if (in.refresh_in_progress || config.server_trust_error) {
            lv_obj_add_state(cloud_manual_pair_button_, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(cloud_manual_pair_button_, LV_STATE_DISABLED);
        }
    }

    // Exactly one view is visible; the main view's controls are not hit-testable under details.
    for (lv_obj_t* main_obj : {cloud_status_card_, cloud_pairing_button_, cloud_secondary_button_}) {
        if (cloud_details_open_) lv_obj_add_flag(main_obj, LV_OBJ_FLAG_HIDDEN);
        else if (main_obj != cloud_secondary_button_ || two) lv_obj_clear_flag(main_obj, LV_OBJ_FLAG_HIDDEN);
    }
    if (cloud_details_open_) lv_obj_clear_flag(cloud_details_panel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(cloud_details_panel_, LV_OBJ_FLAG_HIDDEN);

    if (cloud_pairing_timer_ != nullptr) {
        if (current_page_ == SettingsPage::kDeviceCloud && !in.refresh_failed &&
            (in.refresh_in_progress ||
             (pairing_pending && !bound && cloud_pairing_error_.empty()))) {
            lv_timer_resume(cloud_pairing_timer_);
            lv_timer_reset(cloud_pairing_timer_);
        } else {
            lv_timer_pause(cloud_pairing_timer_);
        }
    }
}

void SettingsApp::ShowDeviceCloudUnbindDialog() {
    if (cloud_unbind_dialog_ != nullptr) {
        return;
    }

    cloud_unbind_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(cloud_unbind_dialog_);
    lv_obj_set_size(cloud_unbind_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cloud_unbind_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cloud_unbind_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(cloud_unbind_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* box = lv_obj_create(cloud_unbind_dialog_);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 288, 154);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_pad_all(box, 14, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(box, "解除与服务的连接？");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    auto* message = CreateSettingLabel(
        box, "解除后将无法在服务端控制此设备，需要重新配对才能恢复。", true);
    lv_obj_set_width(message, 252);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 34);

    auto* cancel = lv_btn_create(box);
    lv_obj_set_size(cancel, 108, 34);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel, 6, 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    auto* cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_set_style_text_color(cancel_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cancel_label, &phone_font_14, 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->CloseDeviceCloudUnbindDialog();
    }, LV_EVENT_CLICKED, this);

    auto* confirm = lv_btn_create(box);
    lv_obj_set_size(confirm, 108, 34);
    lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(confirm, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_radius(confirm, 6, 0);
    lv_obj_set_style_shadow_width(confirm, 0, 0);
    auto* confirm_label = lv_label_create(confirm);
    lv_label_set_text(confirm_label, "确认解除");
    lv_obj_set_style_text_color(confirm_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(confirm_label, &phone_font_14, 0);
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->CloseDeviceCloudUnbindDialog();
        self->UnbindDeviceCloud();
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::CloseDeviceCloudUnbindDialog() {
    if (cloud_unbind_dialog_ != nullptr && lv_obj_is_valid(cloud_unbind_dialog_)) {
        lv_obj_delete(cloud_unbind_dialog_);
    }
    cloud_unbind_dialog_ = nullptr;
}

void SettingsApp::UnbindDeviceCloud() {
    auto* wifi = context_ != nullptr ? context_->services().wifi() : nullptr;
    auto* service = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    if (wifi == nullptr || wifi->GetStatus() != WiFiStatus::kConnected) {
        ui_->ShowToastUnlocked("需联网后才能解除绑定");
        return;
    }
    bool expected = false;
    if (service == nullptr || !cloud_refresh_guard_ ||
        !cloud_refresh_guard_->refresh_in_progress.compare_exchange_strong(expected, true)) {
        ui_->ShowToastUnlocked("设备服务忙碌");
        return;
    }
    auto* payload = new CloudUnbindPayload;
    payload->guard = cloud_refresh_guard_;
    payload->service = service;
    payload->generation = cloud_refresh_guard_->refresh_generation.load();
    if (xTaskCreate(CloudUnbindTask, "cloud_unbind", 6144, payload, 3, nullptr) != pdPASS) {
        cloud_refresh_guard_->refresh_in_progress.store(false);
        delete payload;
        ui_->ShowToastUnlocked("无法启动解绑任务");
    }
}

void SettingsApp::RefreshDeviceCloud() {
    auto* wifi = context_ != nullptr ? context_->services().wifi() : nullptr;
    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    if (device_cloud == nullptr || !cloud_refresh_guard_) {
        ui_->ShowToastUnlocked("设备服务不可用");
        return;
    }
    if (wifi == nullptr || wifi->GetStatus() != WiFiStatus::kConnected) {
        ui_->ShowToastUnlocked("请先连接 Wi-Fi");
        return;
    }
    bool expected = false;
    if (!cloud_refresh_guard_->refresh_in_progress.compare_exchange_strong(expected, true)) {
        ui_->ShowToastUnlocked("正在连接，请稍候");
        return;
    }

    cloud_pairing_error_.clear();
    cloud_refresh_guard_->refresh_failure.store(SettingsCloudRefreshFailure::kNone);
    // Re-render from the model: the busy flag disables the primary and keeps the timer polling.
    UpdateDeviceCloudPage();

    auto* payload = new CloudRefreshPayload;
    payload->guard = cloud_refresh_guard_;
    payload->service = device_cloud;
    payload->generation = cloud_refresh_guard_->refresh_generation.load();

    const BaseType_t ret = xTaskCreate(
        CloudRefreshTask, "cloud_refresh", 6144, payload, 3, nullptr);
    if (ret != pdPASS) {
        cloud_refresh_guard_->refresh_failure.store(SettingsCloudRefreshFailure::kTaskStart);
        cloud_refresh_guard_->refresh_in_progress.store(false);
        delete payload;
        UpdateDeviceCloudPage();
        ui_->ShowToastUnlocked("无法开始连接，请稍后重试");
    }
}

void SettingsApp::OnDeviceCloudRefreshComplete(bool ok,
                                               const rodakos::DeviceCloudConfig& config,
                                               const std::string& error,
                                               uint32_t generation) {
    if (ui_ == nullptr) {
        return;
    }
    if (!cloud_refresh_guard_) {
        return;
    }
    if (generation != cloud_refresh_guard_->refresh_generation.load()) {
        return;
    }

    cloud_refresh_guard_->refresh_in_progress.store(false);
    const bool pairing_pending = config.has_pairing_request &&
        rodakos::ClassifyDevicePairingStatus(config.pairing_status) ==
            rodakos::DevicePairingStatus::kPending;
    auto* service = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    const auto diagnostic = service != nullptr ? service->diagnostic() : rodakos::CloudDiagnosticCode::kRefreshFailed;
    const bool waiting = !ok && pairing_pending && IsExpectedPairingWait(error);
    cloud_pairing_error_ = (!ok && !waiting) ? CloudDiagnosticTitleZh(diagnostic) : std::string();
    UpdateDeviceCloudPage();
    if (ok && config.has_aiot_config) {
        if (context_ != nullptr) {
            context_->services().NotifyDeviceCloudBound();
        }
        ui_->ShowToastUnlocked(diagnostic == rodakos::CloudDiagnosticCode::kReady
                                  ? "设备连接已更新" : CloudDiagnosticTitleZh(diagnostic));
    } else if (!ok && !waiting) {
        ui_->ShowToastUnlocked(CloudDiagnosticTitleZh(diagnostic));
    }
}

void SettingsApp::OnDeviceCloudUnbindComplete(bool ok,
                                              const std::string&,
                                              uint32_t generation) {
    if (ui_ == nullptr || !cloud_refresh_guard_ ||
        generation != cloud_refresh_guard_->refresh_generation.load()) {
        return;
    }
    cloud_refresh_guard_->refresh_in_progress.store(false);
    cloud_pairing_error_.clear();
    if (ok) {
        cloud_pairing_code_.clear();
    }
    UpdateDeviceCloudPage();
    if (ok && context_ != nullptr) {
        context_->services().NotifyDeviceCloudUnbound();
    }
    ui_->ShowToastUnlocked(ok ? "设备已解除绑定" : "设备解绑失败，请检查网络后重试");
}

void SettingsApp::ShowCloudProvisioningUrlDialog() {
    if (cloud_url_dialog_ != nullptr) {
        return;
    }

    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    rodakos::DeviceCloudConfig config;
    if (device_cloud != nullptr) {
        device_cloud->Load(config);
    }
    if (config.provisioning_url.empty()) {
        config.provisioning_url = rodakos::DeviceCloudConfigService::DefaultProvisioningUrl();
    }

    cloud_url_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(cloud_url_dialog_);
    lv_obj_set_size(cloud_url_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cloud_url_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cloud_url_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(cloud_url_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* dialog_box = lv_obj_create(cloud_url_dialog_);
    lv_obj_remove_style_all(dialog_box);
    lv_obj_set_size(dialog_box, 288, 152);
    lv_obj_align(dialog_box, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(dialog_box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(dialog_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dialog_box, 8, 0);
    lv_obj_set_style_pad_all(dialog_box, 12, 0);
    lv_obj_clear_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(dialog_box, "服务地址");
    lv_obj_set_style_text_font(title, &phone_font_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    cloud_url_textarea_ = lv_textarea_create(dialog_box);
    lv_obj_set_size(cloud_url_textarea_, 264, 42);
    lv_obj_align(cloud_url_textarea_, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_textarea_set_one_line(cloud_url_textarea_, true);
    lv_textarea_set_max_length(cloud_url_textarea_, 191);
    lv_textarea_set_text(cloud_url_textarea_, config.provisioning_url.c_str());
    lv_textarea_set_placeholder_text(cloud_url_textarea_, rodakos::DeviceCloudConfigService::DefaultProvisioningUrl());
    lv_obj_set_style_bg_color(cloud_url_textarea_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(cloud_url_textarea_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cloud_url_textarea_, &phone_font_12, 0);
    lv_obj_set_style_border_width(cloud_url_textarea_, 0, 0);
    lv_obj_set_style_radius(cloud_url_textarea_, 6, 0);

    auto* cancel_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(cancel_btn, 126, 30);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_set_style_shadow_width(cancel_btn, 0, 0);

    auto* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_set_style_text_color(cancel_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cancel_label, &phone_font_12, 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->CloseCloudProvisioningUrlDialog();
    }, LV_EVENT_CLICKED, this);

    auto* save_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(save_btn, 126, 30);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(save_btn, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(save_btn, 6, 0);
    lv_obj_set_style_shadow_width(save_btn, 0, 0);

    auto* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "保存");
    lv_obj_set_style_text_color(save_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(save_label, &phone_font_12, 0);
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        const bool saved = self->SaveCloudProvisioningUrl(
            TrimCloudUrl(lv_textarea_get_text(self->cloud_url_textarea_)));
        if (saved) {
            self->CloseCloudProvisioningUrlDialog();
        }
    }, LV_EVENT_CLICKED, this);

    soft_keyboard_.Show(cloud_url_textarea_);
}

void SettingsApp::CloseCloudProvisioningUrlDialog() {
    soft_keyboard_.Hide();
    if (cloud_url_dialog_ != nullptr && lv_obj_is_valid(cloud_url_dialog_)) {
        lv_obj_delete(cloud_url_dialog_);
    }
    cloud_url_dialog_ = nullptr;
    cloud_url_textarea_ = nullptr;
}

void SettingsApp::CloseCloudProvisioningUrlDialogAsync() {
    lv_async_call([](void* user_data) {
        auto* self = static_cast<SettingsApp*>(user_data);
        if (self != nullptr) {
            self->CloseCloudProvisioningUrlDialog();
        }
    }, this);
}

bool SettingsApp::SaveCloudProvisioningUrl(const std::string& url) {
    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    if (device_cloud == nullptr) {
        return false;
    }
    const auto result = device_cloud->SaveProvisioningUrl(url);
    if (result != rodakos::ProvisioningUrlSaveResult::kSaved &&
        result != rodakos::ProvisioningUrlSaveResult::kUnchanged) {
        ui_->ShowToastUnlocked("Failed to save provisioning endpoint");
        return false;
    }
    ui_->ShowToastUnlocked(result == rodakos::ProvisioningUrlSaveResult::kUnchanged
                               ? "服务地址未变更"
                               : "服务地址已保存");
    UpdateDeviceCloudPage();
    return true;
}
