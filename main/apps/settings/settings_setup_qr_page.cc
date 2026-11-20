#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_os/setup_qr_scan_service.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <cstring>
#include <string>

// 扫码配网全屏页：扫码 → 确认 → 结果。状态全部来自 SetupQrScanService 的快照，界面只在
// revision 变化时重绘；扫码中每 500 ms 刷新已识别帧数。
namespace {

lv_obj_t* CreateQrActionButton(lv_obj_t* parent, const char* text, bool primary, lv_coord_t y) {
    auto* button = lv_btn_create(parent);
    lv_obj_set_size(button, 264, 36);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(button, primary ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(button, 7, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, primary ? lv_color_white() : rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    lv_obj_center(label);
    return button;
}

// Codes come from docs/qr-provisioning.md; unknown codes fall back to a generic message.
const char* FailureMessage(const std::string& code) {
    struct Entry {
        const char* code;
        const char* message;
    };
    static constexpr Entry kMessages[] = {
        {"qr_camera_unavailable", "无法打开相机，请稍后重试"},
        {"qr_camera_busy", "相机正在被其他功能使用，请先关闭相机或远程画面"},
        {"qr_out_of_memory", "设备内存不足，请稍后重试"},
        {"qr_task_start_failed", "设备内存不足，请稍后重试"},
        {"qr_scan_timeout", "没有识别到二维码。请把二维码放在相机前 15–30 厘米处"},
        {"qr_not_provisioning", "这不是配网二维码"},
        {"qr_version_unsupported", "二维码版本过新，请升级设备固件"},
        {"qr_syntax_invalid", "二维码内容格式错误"},
        {"qr_ssid_invalid", "二维码中的 Wi-Fi 名称无效"},
        {"qr_auth_unsupported", "不支持该 Wi-Fi 加密方式"},
        {"qr_password_invalid", "二维码中的 Wi-Fi 密码无效"},
        {"qr_url_invalid", "服务器地址无效，必须是 https 域名"},
        {"qr_url_insecure", "服务器地址未加密（http），已拒绝"},
        {"qr_url_legacy", "不支持旧版语音服务地址"},
        {"qr_server_pinned", "设备已固定服务器证书，不能扫码更换服务器。请先解除绑定或使用 USB"},
        {"qr_storage_failed", "保存失败，配置未改动，请重试"},
        {"qr_state_uncertain", "保存未完成，请重启设备后重新配置"},
    };
    for (const auto& entry : kMessages) {
        if (code == entry.code) return entry.message;
    }
    return "扫码配置失败，请重试";
}

}  // namespace

using namespace rodakos_settings;

void SettingsApp::ShowSetupQr() {
    auto* scan = context_ != nullptr ? context_->services().setup_qr_scan() : nullptr;
    if (scan == nullptr || setup_qr_overlay_ != nullptr) return;
    setup_qr_overlay_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(setup_qr_overlay_);
    lv_obj_set_size(setup_qr_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(setup_qr_overlay_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(setup_qr_overlay_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(setup_qr_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    setup_qr_revision_ = UINT32_MAX;
    setup_qr_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
        if (self != nullptr) self->UpdateSetupQr();
    }, 500, this);
    scan->StartScan();
    UpdateSetupQr();
}

void SettingsApp::CloseSetupQr() {
    TeardownSetupQr();
    if (current_page_ == SettingsPage::kDeviceCloud) RefreshDeviceCloud();
}

void SettingsApp::TeardownSetupQr() {
    auto* scan = context_ != nullptr ? context_->services().setup_qr_scan() : nullptr;
    if (scan != nullptr) {
        const auto phase = scan->Snapshot().phase;
        // Saving finishes on its own; cancelling then would only hide the result.
        if (phase != rodakos::SetupQrScanPhase::kSaving) scan->Cancel();
    }
    if (setup_qr_timer_ != nullptr) {
        lv_timer_delete(setup_qr_timer_);
        setup_qr_timer_ = nullptr;
    }
    if (setup_qr_overlay_ != nullptr && lv_obj_is_valid(setup_qr_overlay_)) {
        lv_obj_delete(setup_qr_overlay_);
    }
    setup_qr_overlay_ = nullptr;
    setup_qr_body_ = nullptr;
    setup_qr_progress_ = nullptr;
}

void SettingsApp::UpdateSetupQr() {
    auto* scan = context_ != nullptr ? context_->services().setup_qr_scan() : nullptr;
    if (scan == nullptr || setup_qr_overlay_ == nullptr) return;
    const auto snapshot = scan->Snapshot();
    using Phase = rodakos::SetupQrScanPhase;
    if (snapshot.revision == setup_qr_revision_) {
        if (snapshot.phase == Phase::kScanning && setup_qr_progress_ != nullptr) {
            lv_label_set_text_fmt(setup_qr_progress_, "已检查 %u 帧",
                                  static_cast<unsigned>(snapshot.frames_scanned));
        }
        return;
    }
    setup_qr_revision_ = snapshot.revision;
    setup_qr_progress_ = nullptr;
    if (setup_qr_body_ != nullptr) lv_obj_delete(setup_qr_body_);
    setup_qr_body_ = lv_obj_create(setup_qr_overlay_);
    lv_obj_remove_style_all(setup_qr_body_);
    lv_obj_set_size(setup_qr_body_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(setup_qr_body_, 12, 0);
    lv_obj_clear_flag(setup_qr_body_, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(setup_qr_body_, "扫码配置");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    auto* message = CreateSettingLabel(setup_qr_body_, "", true);
    lv_obj_set_width(message, 272);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 40);

    const char* close_text = "取消";
    switch (snapshot.phase) {
        case Phase::kIdle:
        case Phase::kStarting:
            lv_label_set_text(message, "正在打开相机…");
            break;
        case Phase::kScanning:
            lv_label_set_text(message,
                              "把控制台生成的配网二维码对准设备相机，\n保持 15–30 厘米，等待识别。");
            setup_qr_progress_ = CreateSettingLabel(setup_qr_body_, "", true);
            lv_obj_align(setup_qr_progress_, LV_ALIGN_TOP_MID, 0, 140);
            lv_label_set_text_fmt(setup_qr_progress_, "已检查 %u 帧",
                                  static_cast<unsigned>(snapshot.frames_scanned));
            break;
        case Phase::kConfirm: {
            lv_label_set_text(message, "确认使用以下配置？");
            std::string details = "Wi-Fi：" + snapshot.ssid +
                (snapshot.open_network ? "（开放网络）" : "");
            details += snapshot.kind == rodakos::SetupQrKind::kRodak
                ? "\n服务器：" + snapshot.server
                : "\n服务器：保持不变";
            auto* info = CreateSettingLabel(setup_qr_body_, details.c_str());
            lv_obj_set_width(info, 272);
            lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 96);
            auto* confirm = CreateQrActionButton(setup_qr_body_, "确认并保存", true, 282);
            lv_obj_add_event_cb(confirm, [](lv_event_t* e) {
                auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
                auto* service = self->context_->services().setup_qr_scan();
                if (service != nullptr) service->Confirm();
            }, LV_EVENT_CLICKED, this);
            break;
        }
        case Phase::kSaving:
            lv_label_set_text(message, "正在保存并连接 Wi-Fi…");
            break;
        case Phase::kCompleted:
            close_text = "完成";
            if (snapshot.result == rodakos::SetupQrApplyStatus::kServerChanged) {
                lv_label_set_text(message,
                                  "已保存。连上 Wi-Fi 后设备会重启并连接新服务器，\n"
                                  "然后在服务端输入屏幕上的配对码完成绑定。");
            } else if (snapshot.result == rodakos::SetupQrApplyStatus::kServerUnchanged) {
                lv_label_set_text(message, "Wi-Fi 已保存。服务器未变化，绑定保持不变。");
            } else {
                lv_label_set_text(message, "Wi-Fi 已保存，正在连接。");
            }
            break;
        case Phase::kFailed: {
            lv_label_set_text(message, FailureMessage(snapshot.code));
            const bool retryable = snapshot.code != "qr_server_pinned" &&
                                   snapshot.code != "qr_state_uncertain";
            if (retryable) {
                auto* retry = CreateQrActionButton(setup_qr_body_, "重新扫码", true, 282);
                lv_obj_add_event_cb(retry, [](lv_event_t* e) {
                    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
                    auto* service = self->context_->services().setup_qr_scan();
                    if (service != nullptr) service->StartScan();
                }, LV_EVENT_CLICKED, this);
            }
            close_text = "关闭";
            break;
        }
    }

    if (snapshot.phase == Phase::kSaving) return;
    auto* close = CreateQrActionButton(setup_qr_body_, close_text, false, 330);
    lv_obj_add_event_cb(close, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        // 关闭会删除本按钮所在的界面，放到下一轮事件循环执行
        lv_async_call([](void* user_data) {
            static_cast<SettingsApp*>(user_data)->CloseSetupQr();
        }, self);
    }, LV_EVENT_CLICKED, this);
}
