#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/camera_service.h"
#include "phone_os/device_cloud_config.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_os/setup_qr_scan_service.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// 扫码配网全屏页：扫码 → 确认 → 结果。状态全部来自 SetupQrScanService 的快照，界面只在
// revision 变化时重建；扫码中每 150 ms 刷新相机预览和已检查帧数。
namespace {

// The display is 320x240 landscape and the overlay does not scroll: every control must fit.
constexpr lv_coord_t kPad = 8;
constexpr lv_coord_t kButtonWidth = 148;
constexpr lv_coord_t kButtonHeight = 36;
constexpr lv_coord_t kContentTop = 26;
constexpr lv_coord_t kPreviewWidth = 200;
constexpr lv_coord_t kPreviewHeight = 150;
constexpr uint32_t kRefreshMs = 150;

lv_obj_t* CreateQrActionButton(lv_obj_t* parent, const char* text, bool primary, lv_align_t align) {
    auto* button = lv_btn_create(parent);
    lv_obj_set_size(button, kButtonWidth, kButtonHeight);
    lv_obj_align(button, align, 0, 0);
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

lv_obj_t* CreateWrappedLabel(lv_obj_t* parent, const char* text, lv_coord_t width, bool secondary) {
    auto* label = rodakos_settings::CreateSettingLabel(parent, text, secondary);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
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
    }, kRefreshMs, this);
    scan->StartScan();
    UpdateSetupQr();
}

void SettingsApp::CloseSetupQr() {
    TeardownSetupQr();
    // Re-render from NVS only. A refresh would start a pairing request against the saved URL.
    if (current_page_ == SettingsPage::kDeviceCloud) UpdateDeviceCloudPage();
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
    DetachSetupQrPreview();
    if (setup_qr_overlay_ != nullptr && lv_obj_is_valid(setup_qr_overlay_)) {
        lv_obj_delete(setup_qr_overlay_);
    }
    setup_qr_overlay_ = nullptr;
    setup_qr_body_ = nullptr;
    setup_qr_progress_ = nullptr;
    ReleaseSetupQrPreviewPixels();
}

void SettingsApp::DetachSetupQrPreview() {
    // LVGL draws straight from the pixel vector: detach the source before the vector changes.
    if (setup_qr_preview_image_ != nullptr && lv_obj_is_valid(setup_qr_preview_image_)) {
        lv_image_set_src(setup_qr_preview_image_, nullptr);
    }
    setup_qr_preview_image_ = nullptr;
    setup_qr_preview_placeholder_ = nullptr;
}

void SettingsApp::ReleaseSetupQrPreviewPixels() {
    std::vector<uint8_t>().swap(setup_qr_preview_pixels_);
    std::memset(&setup_qr_preview_dsc_, 0, sizeof(setup_qr_preview_dsc_));
    setup_qr_preview_sequence_ = 0;
}

void SettingsApp::UpdateSetupQrPreview() {
    auto* camera = context_ != nullptr ? context_->services().camera() : nullptr;
    if (camera == nullptr || setup_qr_preview_image_ == nullptr) return;
    rodakos::CameraFrame frame;
    if (!camera->GetLatestFrame(frame) || frame.sequence == setup_qr_preview_sequence_) return;
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 2 ||
        frame.rgb565.size() < static_cast<size_t>(frame.stride) * frame.height) {
        return;
    }
    lv_image_set_src(setup_qr_preview_image_, nullptr);
    setup_qr_preview_pixels_ = std::move(frame.rgb565);
    setup_qr_preview_sequence_ = frame.sequence;
    std::memset(&setup_qr_preview_dsc_, 0, sizeof(setup_qr_preview_dsc_));
    setup_qr_preview_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    setup_qr_preview_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    setup_qr_preview_dsc_.header.w = frame.width;
    setup_qr_preview_dsc_.header.h = frame.height;
    setup_qr_preview_dsc_.header.stride = frame.stride;
    setup_qr_preview_dsc_.data = setup_qr_preview_pixels_.data();
    setup_qr_preview_dsc_.data_size = setup_qr_preview_pixels_.size();
    lv_image_set_src(setup_qr_preview_image_, &setup_qr_preview_dsc_);
    // Fit the whole frame without cropping, so the user aims with exactly what the decoder sees.
    const int32_t scale_w = (kPreviewWidth * LV_SCALE_NONE) / frame.width;
    const int32_t scale_h = (kPreviewHeight * LV_SCALE_NONE) / frame.height;
    lv_image_set_scale(setup_qr_preview_image_,
                       static_cast<uint32_t>(std::max<int32_t>(1, std::min(scale_w, scale_h))));
    lv_obj_center(setup_qr_preview_image_);
    if (setup_qr_preview_placeholder_ != nullptr) {
        lv_obj_add_flag(setup_qr_preview_placeholder_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsApp::UpdateSetupQr() {
    auto* scan = context_ != nullptr ? context_->services().setup_qr_scan() : nullptr;
    if (scan == nullptr || setup_qr_overlay_ == nullptr) return;
    const auto snapshot = scan->Snapshot();
    using Phase = rodakos::SetupQrScanPhase;
    if (snapshot.revision == setup_qr_revision_) {
        if (snapshot.phase == Phase::kScanning) {
            UpdateSetupQrPreview();
            if (setup_qr_progress_ != nullptr) {
                lv_label_set_text_fmt(setup_qr_progress_, "已检查 %u 帧",
                                      static_cast<unsigned>(snapshot.frames_scanned));
            }
        }
        return;
    }
    setup_qr_revision_ = snapshot.revision;
    setup_qr_progress_ = nullptr;
    DetachSetupQrPreview();
    if (setup_qr_body_ != nullptr) lv_obj_delete(setup_qr_body_);
    ReleaseSetupQrPreviewPixels();
    setup_qr_body_ = lv_obj_create(setup_qr_overlay_);
    lv_obj_remove_style_all(setup_qr_body_);
    lv_obj_set_size(setup_qr_body_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(setup_qr_body_, kPad, 0);
    lv_obj_clear_flag(setup_qr_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(setup_qr_body_);

    auto* title = CreateSettingLabel(setup_qr_body_, "扫码配置");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Fallback when the code will not scan. It opens the existing WiFi list and URL editor,
    // so both paths keep one set of validation rules.
    if (snapshot.phase != Phase::kSaving && snapshot.phase != Phase::kCompleted) {
        auto* manual = lv_btn_create(setup_qr_body_);
        lv_obj_remove_style_all(manual);
        lv_obj_set_size(manual, 76, 32);
        lv_obj_align(manual, LV_ALIGN_TOP_RIGHT, kPad, -kPad);
        auto* manual_label = lv_label_create(manual);
        lv_label_set_text(manual_label, "手动输入");
        lv_obj_set_style_text_color(manual_label, rodakos_theme_primary(), 0);
        lv_obj_set_style_text_font(manual_label, &phone_font_14, 0);
        lv_obj_align(manual_label, LV_ALIGN_RIGHT_MID, -kPad, 0);
        lv_obj_add_event_cb(manual, [](lv_event_t* e) {
            auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
            lv_async_call([](void* user_data) {
                static_cast<SettingsApp*>(user_data)->ShowSetupQrManualEntry();
            }, self);
        }, LV_EVENT_CLICKED, this);
    }

    const lv_coord_t content_width = lv_obj_get_content_width(setup_qr_body_);
    const char* close_text = "取消";
    lv_obj_t* primary = nullptr;
    switch (snapshot.phase) {
        case Phase::kIdle:
        case Phase::kStarting:
        case Phase::kScanning: {
            auto* box = lv_obj_create(setup_qr_body_);
            lv_obj_remove_style_all(box);
            lv_obj_set_size(box, kPreviewWidth, kPreviewHeight);
            lv_obj_align(box, LV_ALIGN_TOP_LEFT, 0, kContentTop);
            lv_obj_set_style_bg_color(box, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(box, 6, 0);
            lv_obj_set_style_clip_corner(box, true, 0);
            lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
            setup_qr_preview_image_ = lv_image_create(box);
            lv_obj_center(setup_qr_preview_image_);
            setup_qr_preview_placeholder_ = lv_label_create(box);
            lv_label_set_text(setup_qr_preview_placeholder_, "正在打开相机…");
            lv_obj_set_style_text_color(setup_qr_preview_placeholder_, rodakos_theme_text_secondary(), 0);
            lv_obj_set_style_text_font(setup_qr_preview_placeholder_, &phone_font_14, 0);
            lv_obj_center(setup_qr_preview_placeholder_);

            const lv_coord_t side_width = content_width - kPreviewWidth - kPad;
            auto* hint = CreateWrappedLabel(setup_qr_body_, "把配网二维码放进画面中央，距离 15–30 厘米",
                                            side_width, true);
            lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, 0, kContentTop);
            setup_qr_progress_ = CreateWrappedLabel(setup_qr_body_, "", side_width, true);
            lv_obj_align(setup_qr_progress_, LV_ALIGN_TOP_RIGHT, 0, kContentTop + 104);
            lv_label_set_text_fmt(setup_qr_progress_, "已检查 %u 帧",
                                  static_cast<unsigned>(snapshot.frames_scanned));
            if (snapshot.phase == Phase::kScanning) UpdateSetupQrPreview();
            break;
        }
        case Phase::kConfirm: {
            auto* message = CreateWrappedLabel(setup_qr_body_, "确认使用以下配置？", content_width, true);
            lv_obj_align(message, LV_ALIGN_TOP_LEFT, 0, kContentTop);
            std::string details = "Wi-Fi：" + snapshot.ssid +
                (snapshot.open_network ? "（开放网络）" : "");
            details += snapshot.kind == rodakos::SetupQrKind::kRodak
                ? "\n服务器：" + snapshot.server
                : "\n服务器：保持不变";
            auto* info = CreateWrappedLabel(setup_qr_body_, details.c_str(), content_width, false);
            lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, kContentTop + 30);
            if (snapshot.will_restart) {
                auto* note = CreateWrappedLabel(
                    setup_qr_body_, "更换服务器后设备会自动重启，然后在服务端输入屏幕上的配对码完成绑定。",
                    content_width, true);
                lv_obj_set_style_text_color(note, rodakos_theme_warning(), 0);
                lv_obj_align(note, LV_ALIGN_TOP_LEFT, 0, kContentTop + 76);
            }
            primary = CreateQrActionButton(setup_qr_body_,
                                           snapshot.will_restart ? "保存并重启" : "确认并保存", true,
                                           LV_ALIGN_BOTTOM_LEFT);
            lv_obj_add_event_cb(primary, [](lv_event_t* e) {
                auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
                auto* service = self->context_->services().setup_qr_scan();
                if (service != nullptr) service->Confirm();
            }, LV_EVENT_CLICKED, this);
            break;
        }
        case Phase::kSaving: {
            auto* message = CreateWrappedLabel(setup_qr_body_, "正在保存并连接 Wi-Fi…", content_width, true);
            lv_obj_align(message, LV_ALIGN_TOP_LEFT, 0, kContentTop);
            break;
        }
        case Phase::kCompleted: {
            close_text = "完成";
            const char* text = "Wi-Fi 已保存，正在连接。";
            if (snapshot.result == rodakos::SetupQrApplyStatus::kServerChanged) {
                text = "已保存，正在重启…";
            } else if (snapshot.result == rodakos::SetupQrApplyStatus::kServerUnchanged) {
                text = "Wi-Fi 已保存。服务器未变化，绑定保持不变。";
            }
            auto* message = CreateWrappedLabel(setup_qr_body_, text, content_width, false);
            lv_obj_align(message, LV_ALIGN_TOP_LEFT, 0, kContentTop);
            break;
        }
        case Phase::kFailed: {
            auto* message = CreateWrappedLabel(setup_qr_body_, FailureMessage(snapshot.code),
                                               content_width, false);
            lv_obj_align(message, LV_ALIGN_TOP_LEFT, 0, kContentTop);
            const bool retryable = snapshot.code != "qr_server_pinned" &&
                                   snapshot.code != "qr_state_uncertain";
            if (retryable) {
                primary = CreateQrActionButton(setup_qr_body_, "重新扫码", true, LV_ALIGN_BOTTOM_LEFT);
                lv_obj_add_event_cb(primary, [](lv_event_t* e) {
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
    if (snapshot.phase == Phase::kCompleted &&
        snapshot.result == rodakos::SetupQrApplyStatus::kServerChanged) {
        return;
    }
    auto* close = CreateQrActionButton(setup_qr_body_, close_text, false,
                                       primary != nullptr ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_MID);
    lv_obj_add_event_cb(close, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        // 关闭会删除本按钮所在的界面，放到下一轮事件循环执行
        lv_async_call([](void* user_data) {
            static_cast<SettingsApp*>(user_data)->CloseSetupQr();
        }, self);
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::ShowSetupQrManualEntry() {
    if (setup_qr_overlay_ == nullptr) return;
    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    rodakos::DeviceCloudConfig config;
    if (device_cloud != nullptr) device_cloud->Load(config);
    const bool pinned = !config.server_trust.empty() || config.server_trust_pending ||
                        config.server_trust_error;
    // Stop the camera first; the chooser replaces the scan view.
    TeardownSetupQr();

    setup_qr_overlay_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(setup_qr_overlay_);
    lv_obj_set_size(setup_qr_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(setup_qr_overlay_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(setup_qr_overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(setup_qr_overlay_, kPad, 0);
    lv_obj_clear_flag(setup_qr_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    setup_qr_body_ = setup_qr_overlay_;

    auto* title = CreateSettingLabel(setup_qr_overlay_, "手动输入");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    struct Row {
        const char* name;
        std::string value;
        bool enabled;
        lv_event_cb_t action;
    };
    auto* wifi = context_->services().wifi();
    std::string ssid = wifi_config_.GetSavedSSID();
    if (ssid.empty()) ssid = "未设置";
    std::string url = config.provisioning_url.empty() ? std::string("未设置") : config.provisioning_url;
    if (pinned) url += "（已固定）";
    const Row rows[] = {
        {"Wi-Fi 网络", ssid, wifi != nullptr, [](lv_event_t* e) {
             auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
             lv_async_call([](void* user_data) {
                 auto* app = static_cast<SettingsApp*>(user_data);
                 app->TeardownSetupQr();
                 app->ShowPage(SettingsPage::kWiFiList);
             }, self);
         }},
        {"服务器地址", url, !pinned, [](lv_event_t* e) {
             auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
             lv_async_call([](void* user_data) {
                 auto* app = static_cast<SettingsApp*>(user_data);
                 app->TeardownSetupQr();
                 app->ShowCloudProvisioningUrlDialog();
             }, self);
         }},
    };
    lv_coord_t y = kContentTop + 4;
    for (const Row& row : rows) {
        auto* item = lv_btn_create(setup_qr_overlay_);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_PCT(100), 48);
        lv_obj_align(item, LV_ALIGN_TOP_LEFT, 0, y);
        lv_obj_set_style_bg_color(item, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_style_pad_hor(item, 12, 0);
        auto* name = CreateSettingLabel(item, row.name, false);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        auto* value = CreateSettingLabel(item, row.value.c_str(), true);
        lv_obj_set_width(value, 150);
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(value, &phone_font_12, 0);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, row.enabled ? -16 : 0, 0);
        if (row.enabled) {
            auto* arrow = CreateSettingLabel(item, ">", true);
            lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_add_event_cb(item, row.action, LV_EVENT_CLICKED, this);
        } else {
            lv_obj_add_state(item, LV_STATE_DISABLED);
        }
        y += 56;
    }

    auto* back = CreateQrActionButton(setup_qr_overlay_, "返回", false, LV_ALIGN_BOTTOM_MID);
    lv_obj_add_event_cb(back, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        lv_async_call([](void* user_data) {
            static_cast<SettingsApp*>(user_data)->CloseSetupQr();
        }, self);
    }, LV_EVENT_CLICKED, this);
}
