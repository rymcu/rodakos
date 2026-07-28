#include "apps/wol/wol_device_store.h"

#include "settings.h"

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "WolDevices";
constexpr const char* kNamespace = "wol";
constexpr const char* kDevicesKey = "devices";

WolDeviceLoadResult LoadFailure(WolDeviceLoadStatus status) {
    WolDeviceLoadResult result;
    result.status = status;
    return result;
}
}

WolDeviceLoadResult WolDeviceStore::Load() {
    loaded_ = true;
    write_allowed_ = false;
    reset_allowed_ = false;

    Settings settings(kNamespace, false);
    std::string encoded;
    const SettingsStringReadStatus read_status =
        settings.ReadString(kDevicesKey, encoded, kWolDeviceJsonMaxBytes);
    if (read_status == SettingsStringReadStatus::kNotFound) {
        write_allowed_ = true;
        return {.status = WolDeviceLoadStatus::kMissing, .devices = {}, .write_allowed = true};
    }
    if (read_status == SettingsStringReadStatus::kTooLarge) {
        reset_allowed_ = true;
        return LoadFailure(WolDeviceLoadStatus::kTooLarge);
    }
    if (read_status != SettingsStringReadStatus::kOk) {
        reset_allowed_ = read_status == SettingsStringReadStatus::kTypeMismatch;
        return LoadFailure(read_status == SettingsStringReadStatus::kTypeMismatch
                               ? WolDeviceLoadStatus::kCorrupt
                               : WolDeviceLoadStatus::kStorageError);
    }

    WolDeviceDecodeResult decoded = DecodeWolDevices(encoded);
    if (decoded.status == WolDeviceCodecStatus::kUnsupportedVersion) {
        return LoadFailure(WolDeviceLoadStatus::kUnsupportedVersion);
    }
    if (decoded.status == WolDeviceCodecStatus::kTooLarge) {
        return LoadFailure(WolDeviceLoadStatus::kTooLarge);
    }
    if (decoded.status != WolDeviceCodecStatus::kOk) {
        reset_allowed_ = true;
        return LoadFailure(WolDeviceLoadStatus::kCorrupt);
    }

    write_allowed_ = true;
    ESP_LOGI(TAG, "Loaded %u Wake-on-LAN devices",
             static_cast<unsigned>(decoded.devices.size()));
    return {.status = WolDeviceLoadStatus::kLoaded,
            .devices = std::move(decoded.devices),
            .write_allowed = true};
}

bool WolDeviceStore::Save(const std::vector<WolDevice>& devices) {
    if (!loaded_ || !write_allowed_) {
        return false;
    }
    WolDeviceEncodeResult encoded = EncodeWolDevices(devices);
    if (encoded.status != WolDeviceCodecStatus::kOk) {
        return false;
    }

    Settings settings(kNamespace, true);
    if (settings.WriteString(kDevicesKey, encoded.json) != SettingsStringWriteStatus::kOk ||
        !settings.Commit()) {
        write_allowed_ = false;
        ESP_LOGE(TAG, "Saving Wake-on-LAN devices failed; writes disabled for this session");
        return false;
    }
    ESP_LOGI(TAG, "Saved %u Wake-on-LAN devices", static_cast<unsigned>(devices.size()));
    return true;
}

bool WolDeviceStore::Reset() {
    if (!loaded_ || !reset_allowed_) {
        return false;
    }
    const WolDeviceEncodeResult encoded = EncodeWolDevices({});
    if (encoded.status != WolDeviceCodecStatus::kOk) {
        return false;
    }

    Settings settings(kNamespace, true);
    if (settings.WriteString(kDevicesKey, encoded.json) != SettingsStringWriteStatus::kOk ||
        !settings.Commit()) {
        reset_allowed_ = false;
        ESP_LOGE(TAG, "Resetting Wake-on-LAN devices failed");
        return false;
    }
    write_allowed_ = true;
    reset_allowed_ = false;
    ESP_LOGI(TAG, "Reset Wake-on-LAN devices");
    return true;
}

}  // namespace rodakos
