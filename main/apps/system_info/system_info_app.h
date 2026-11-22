#pragma once

#include "phone_os/phone_app.h"
#include "rodakos_adapters/file_service.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class AudioFocusService;
class AudioOutputService;
class AudioService;
class BatteryStateProvider;
class ButtonBindingService;
class CameraService;
class MotionService;
class MusicPlayerService;
class VoiceAssistantService;
class VoiceWakeService;
class WebFileSystemService;
}  // namespace rodakos

// 关于本机: grouped list (identity, resources, network & power, services, firmware) with a
// per-service detail page. See .codex-temp/design/system-info-ui.html for the layout.
class SystemInfoApp final : public PhoneApp {
public:
    // Rows of the 系统服务 group, plus the 开发信息 row under 固件; each opens a detail page.
    enum class Service : uint8_t {
        kCamera,
        kAudio,
        kWake,
        kAssistant,
        kMotion,
        kButtons,
        kWebFiles,
        kDeveloper,
        kCount,
    };

    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override;
    bool OnBackRequested() override;
    bool OnThemeChanged(PhoneAppContext& context) override;

    void Refresh();
    void OpenDetail(Service service);
    void CloseDetail();

private:
    struct ResourceRow {
        lv_obj_t* value = nullptr;
        lv_obj_t* bar = nullptr;
    };
    struct ServiceRow {
        lv_obj_t* dot = nullptr;
        lv_obj_t* label = nullptr;
    };
    // Shown in a service row and at the top of its detail page.
    enum class Tone : uint8_t { kIdle, kActive, kOff, kWarning, kError };
    struct Status {
        const char* word = "—";
        Tone tone = Tone::kIdle;
    };
    static constexpr int kDetailRows = 4;

    void CreateUi();
    void CreateMainList();
    void DestroyUi();
    void ResetUiPointers();
    void BindServices(PhoneAppContext& context);
    void RefreshResources();
    void RefreshNetworkPower();
    void RefreshServices();
    void RefreshDetail();
    Status ServiceStatus(Service service);
    static void ApplyStatus(lv_obj_t* dot, lv_obj_t* label, Status status);
    void SetDetailRow(int index, const char* key, const char* value, bool error = false);
    // Card I/O (mount, f_getfree) runs on a short worker; the LVGL timer only reads the result.
    struct StorageSnapshot {
        std::mutex mutex;
        bool checked = false;
        bool mounted = false;
        rodakos::FileService::Capacity capacity{};
        int64_t updated_ms = 0;
        std::atomic<bool> probing{false};
    };
    void RequestStorageProbe(bool allow_mount);
    bool StorageMounted() const;
    void NavigateBack();
    void NavigateHome();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::FileService* file_service_ = nullptr;
    std::shared_ptr<StorageSnapshot> storage_snapshot_;
    rodakos::AudioFocusService* audio_focus_service_ = nullptr;
    rodakos::AudioOutputService* audio_output_service_ = nullptr;
    rodakos::AudioService* audio_service_ = nullptr;
    rodakos::BatteryStateProvider* battery_ = nullptr;
    rodakos::ButtonBindingService* button_service_ = nullptr;
    rodakos::CameraService* camera_service_ = nullptr;
    rodakos::MotionService* motion_service_ = nullptr;
    rodakos::MusicPlayerService* music_player_service_ = nullptr;
    rodakos::VoiceAssistantService* voice_assistant_service_ = nullptr;
    rodakos::VoiceWakeService* voice_wake_service_ = nullptr;
    rodakos::WebFileSystemService* web_files_service_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;

    ResourceRow sram_;
    ResourceRow psram_;
    ResourceRow storage_;
    lv_obj_t* wifi_name_ = nullptr;
    lv_obj_t* wifi_detail_ = nullptr;
    lv_obj_t* battery_value_ = nullptr;
    lv_obj_t* uptime_value_ = nullptr;
    ServiceRow services_[static_cast<int>(Service::kCount)] = {};

    // Detail page: built on open, deleted on close. The main list stays alive underneath.
    Service detail_service_ = Service::kCount;
    lv_obj_t* detail_ = nullptr;
    lv_obj_t* detail_heading_ = nullptr;
    lv_obj_t* detail_dot_ = nullptr;
    lv_obj_t* detail_status_ = nullptr;
    lv_obj_t* detail_keys_[kDetailRows] = {};
    lv_obj_t* detail_values_[kDetailRows] = {};
    lv_obj_t* detail_raw_ = nullptr;
};

void RegisterSystemInfoApp(PhoneAppRegistry& registry);
