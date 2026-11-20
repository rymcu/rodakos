#pragma once

#include "phone_os/device_cloud_config.h"
#include "phone_os/setup_qr_payload.h"

#include <string>

namespace rodakos {

enum class SetupQrServerTrust {
    kNone,
    kPinned,
    // The trust record cannot be read. Treat it like a pin: a QR code must never replace it.
    kUnreadable,
};

// Persistence used by the QR save transaction. The firmware store wraps WiFiConfig, the shared
// `serial_prov/pending` recovery marker and DeviceCloudConfigService; host tests use a fake.
class SetupQrProvisioningStore {
public:
    virtual ~SetupQrProvisioningStore() = default;

    virtual SetupQrServerTrust ServerTrust() = 0;
    virtual bool LoadWifi(std::string& ssid, std::string& password) = 0;
    virtual bool SaveWifi(const std::string& ssid, const std::string& password) = 0;
    virtual bool ClearWifi() = 0;
    virtual bool SetTransactionPending(bool pending) = 0;
    virtual ProvisioningUrlSaveResult SaveBootstrapUrl(const std::string& url) = 0;
};

enum class SetupQrApplyStatus {
    // Only WiFi changed; reconnect WiFi.
    kWifiSaved,
    // WiFi saved; the bootstrap URL was already active, so the binding and credentials remain.
    kServerUnchanged,
    // WiFi and a new bootstrap URL saved; cached cloud credentials were cleared. Restart cloud.
    kServerChanged,
    kServerPinned,
    // Nothing changed, or every partial write was rolled back.
    kStorageFailed,
    // A rollback failed. The pending marker stays set so the next boot runs the conservative
    // SerialProvisioningService::RecoverPendingTransaction.
    kStateUncertain,
};

// Saves WiFi first, then the bootstrap URL with kPreserveCredentials, inside the same pending
// marker as USB provisioning. On a URL failure the previous WiFi credentials are restored.
SetupQrApplyStatus ApplySetupQrProvisioning(const SetupQrPayload& payload,
                                            SetupQrProvisioningStore& store);

constexpr bool IsSetupQrApplySuccess(SetupQrApplyStatus status) {
    return status == SetupQrApplyStatus::kWifiSaved ||
           status == SetupQrApplyStatus::kServerUnchanged ||
           status == SetupQrApplyStatus::kServerChanged;
}

// Stable code for logs and UI mapping; it never contains payload data.
const char* SetupQrApplyStatusCode(SetupQrApplyStatus status);

#ifdef ESP_PLATFORM
// Firmware store: WiFiConfig NVS, the shared `serial_prov/pending` marker and Device Cloud.
class DeviceSetupQrProvisioningStore final : public SetupQrProvisioningStore {
public:
    explicit DeviceSetupQrProvisioningStore(DeviceCloudConfigService& cloud) : cloud_(cloud) {}

    SetupQrServerTrust ServerTrust() override;
    bool LoadWifi(std::string& ssid, std::string& password) override;
    bool SaveWifi(const std::string& ssid, const std::string& password) override;
    bool ClearWifi() override;
    bool SetTransactionPending(bool pending) override;
    ProvisioningUrlSaveResult SaveBootstrapUrl(const std::string& url) override;

private:
    DeviceCloudConfigService& cloud_;
};
#endif

}  // namespace rodakos
