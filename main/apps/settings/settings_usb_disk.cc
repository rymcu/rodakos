#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_os/web_file_system_service.h"
#include "rodakos_adapters/file_service.h"
#include "usb_msc_mode.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>
#include <new>

namespace {
constexpr const char* TAG = "SettingsUsbDisk";
// Long enough for the user to read the hint page.
constexpr uint32_t kRestartDelayMs = 1500;
// Upper bound for an in-flight web upload to finish its current file.
constexpr int kUploadDrainPolls = 50;

// The card mounts on demand, so the dialog mounts it on a worker (card I/O) and reports back.
struct UsbSdProbeArgs {
    std::shared_ptr<SettingsCloudRefreshGuard> guard;
    rodakos::FileService* files = nullptr;
    bool ready = false;
};

void UsbSdProbeDelivered(void* user_data) {
    auto* args = static_cast<UsbSdProbeArgs*>(user_data);
    SettingsApp* app = args->guard ? args->guard->app.load() : nullptr;
    if (app != nullptr) app->OnUsbDiskSdProbed(args->ready);
    delete args;
}

void UsbSdProbeTask(void* arg) {
    auto* args = static_cast<UsbSdProbeArgs*>(arg);
    args->ready = args->files != nullptr && (args->files->IsMounted() || args->files->Init());
    bool queued = false;
    if (lvgl_port_lock(1000)) {
        queued = lv_async_call(UsbSdProbeDelivered, args) == LV_RESULT_OK;
        lvgl_port_unlock();
    }
    if (!queued) delete args;
    vTaskDelete(nullptr);
}

struct UsbRestartArgs {
    rodakos::WebFileSystemService* web_files = nullptr;
};

// Owned by the system, not by Settings: once the boot request is written, leaving Settings must
// not cancel the restart, or the next ordinary boot would unexpectedly enter USB disk mode.
void UsbRestartTask(void* arg) {
    auto* args = static_cast<UsbRestartArgs*>(arg);
    if (args->web_files != nullptr && args->web_files->IsRunning()) {
        for (int poll = 0; poll < kUploadDrainPolls && args->web_files->GetState().busy; ++poll) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        // httpd_stop waits for in-flight handlers, so the current file is closed before reboot.
        args->web_files->Stop();
    }
    delete args;
    vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
    esp_restart();
}
}  // namespace

using namespace rodakos_settings;
void SettingsApp::ShowUsbDiskDialog() {
    if (usb_disk_dialog_ != nullptr) {
        return;
    }

    const SettingDialog dialog = CreateSettingDialog(212);
    usb_disk_dialog_ = dialog.layer;
    auto* dialog_box = dialog.box;

    auto* files = context_ != nullptr ? context_->services().file_service() : nullptr;
    auto* web_files = context_ != nullptr ? context_->services().web_files() : nullptr;
    const bool web_running = web_files != nullptr && web_files->IsRunning();
    usb_disk_sd_ready_ = false;

    CreateDialogTitle(dialog_box, "进入 USB 磁盘模式？");
    CreateDialogMessage(dialog_box, "设备会重启，把 SD 卡作为 U 盘给电脑使用。", 28);
    usb_disk_sd_label_ = CreateDialogMessage(dialog_box, "正在检查 SD 卡…", 50);

    lv_coord_t hint_y = 68;
    if (web_running) {
        auto* web_line = CreateDialogMessage(dialog_box, "网页传文件运行中，会等当前文件写完后关闭", hint_y);
        lv_obj_set_style_text_color(web_line, rodakos_theme_warning(), 0);
        hint_y += 18;
    }
    CreateDialogMessage(dialog_box, "用完在电脑上安全弹出，再长按 IO10 键回到正常模式。", hint_y);

    auto* enter_btn = CreateDialogButtons(dialog_box, "重启进入", SettingButtonStyle::kPrimary,
        [](lv_event_t* e) {
            static_cast<SettingsApp*>(lv_event_get_user_data(e))->CloseUsbDiskDialog();
        },
        [](lv_event_t* e) {
            static_cast<SettingsApp*>(lv_event_get_user_data(e))->EnterUsbDiskMode();
        },
        this);
    lv_obj_add_state(enter_btn, LV_STATE_DISABLED);
    usb_disk_enter_button_ = enter_btn;

    auto* probe = new (std::nothrow) UsbSdProbeArgs{cloud_refresh_guard_, files, false};
    if (probe == nullptr || !cloud_refresh_guard_ ||
        xTaskCreate(UsbSdProbeTask, "usb_sd_probe", 4096, probe, 3, nullptr) != pdPASS) {
        delete probe;
        OnUsbDiskSdProbed(false);
    }
}

void SettingsApp::CloseUsbDiskDialog() {
    if (usb_disk_dialog_ != nullptr && lv_obj_is_valid(usb_disk_dialog_)) {
        lv_obj_delete(usb_disk_dialog_);
    }
    usb_disk_dialog_ = nullptr;
    usb_disk_sd_label_ = nullptr;
    usb_disk_enter_button_ = nullptr;
}

void SettingsApp::OnUsbDiskSdProbed(bool ready) {
    usb_disk_sd_ready_ = ready;
    if (usb_disk_sd_label_ == nullptr) return;
    lv_label_set_text(usb_disk_sd_label_, ready ? "SD 卡：已插入" : "未检测到 SD 卡，无法进入");
    lv_obj_set_style_text_color(usb_disk_sd_label_,
                                ready ? rodakos_theme_success() : rodakos_theme_error(), 0);
    if (usb_disk_enter_button_ != nullptr) {
        if (ready) lv_obj_clear_state(usb_disk_enter_button_, LV_STATE_DISABLED);
        else lv_obj_add_state(usb_disk_enter_button_, LV_STATE_DISABLED);
    }
}

void SettingsApp::EnterUsbDiskMode() {
    if (!usb_disk_sd_ready_) {
        ui_->ShowToastUnlocked("未检测到 SD 卡");
        return;
    }
    if (!RequestUsbMscModeOnNextBoot()) {
        ui_->ShowToastUnlocked("无法进入 USB 磁盘模式，请重试");
        return;
    }

    CloseUsbDiskDialog();
    ShowUsbDiskEnablePage();
}

void SettingsApp::ShowUsbDiskEnablePage() {
    if (usb_disk_hint_page_ != nullptr) {
        return;
    }

    usb_disk_hint_page_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(usb_disk_hint_page_);
    lv_obj_set_size(usb_disk_hint_page_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(usb_disk_hint_page_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(usb_disk_hint_page_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(usb_disk_hint_page_, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(title, "USB 磁盘模式");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_set_style_text_color(title, rodakos_theme_text_primary(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    auto* icon = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(icon, ICON_STORAGE);
    lv_obj_set_style_text_font(icon, PhoneIconFontLarge(), 0);
    lv_obj_set_style_text_color(icon, rodakos_theme_primary(), 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 52);

    auto* message = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(message,
                      "SD 卡将作为 U 盘出现在电脑上。\n"
                      "此模式下触摸屏不可用。\n\n"
                      "回到正常模式：\n"
                      "1. 在电脑上安全弹出\n"
                      "2. 长按 IO10 键，或按复位键");
    lv_obj_set_width(message, 286);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message, &phone_font_12, 0);
    lv_obj_set_style_text_color(message, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 92);

    auto* footer = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(footer, "正在重启…");
    lv_obj_set_style_text_font(footer, &phone_font_12, 0);
    lv_obj_set_style_text_color(footer, rodakos_theme_primary(), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_move_foreground(usb_disk_hint_page_);
    lv_refr_now(nullptr);

    auto* args = new (std::nothrow) UsbRestartArgs;
    if (args != nullptr) {
        args->web_files = context_ != nullptr ? context_->services().web_files() : nullptr;
        if (xTaskCreate(UsbRestartTask, "usb_restart", 4096, args, 5, nullptr) == pdPASS) return;
        delete args;
    }
    // No task: restart now rather than leave the boot request armed for a later ordinary boot.
    ESP_LOGE(TAG, "Cannot start USB restart task; restarting immediately");
    esp_restart();
}
