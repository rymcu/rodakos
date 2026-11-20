#include "phone_os/setup_qr_provisioning.h"

#include "phone_os/serial_provisioning_service.h"
#include "rodakos_adapters/wifi_config.h"

#include <memory>
#include <new>

namespace rodakos {

SetupQrServerTrust DeviceSetupQrProvisioningStore::ServerTrust() {
    auto config = std::unique_ptr<DeviceCloudConfig>(new (std::nothrow) DeviceCloudConfig);
    // Without a snapshot the pin state is unknown; refuse rather than risk replacing a pin.
    if (config == nullptr) return SetupQrServerTrust::kUnreadable;
    // Load() reports whether AIoT credentials exist, which is false on a fresh device; trust
    // read failures are reported separately in server_trust_error.
    (void)cloud_.Load(*config);
    if (config->server_trust_error) return SetupQrServerTrust::kUnreadable;
    return config->server_trust.empty() && !config->server_trust_pending
               ? SetupQrServerTrust::kNone
               : SetupQrServerTrust::kPinned;
}

bool DeviceSetupQrProvisioningStore::LoadWifi(std::string& ssid, std::string& password) {
    WiFiConfig wifi;
    return wifi.LoadCredentials(ssid, password);
}

bool DeviceSetupQrProvisioningStore::SaveWifi(const std::string& ssid,
                                              const std::string& password) {
    WiFiConfig wifi;
    return wifi.SaveCredentials(ssid, password);
}

bool DeviceSetupQrProvisioningStore::ClearWifi() {
    WiFiConfig wifi;
    return wifi.ClearCredentials();
}

bool DeviceSetupQrProvisioningStore::SetTransactionPending(bool pending) {
    return SerialProvisioningService::SetTransactionPending(pending);
}

ProvisioningUrlSaveResult DeviceSetupQrProvisioningStore::SaveBootstrapUrl(
    const std::string& url) {
    return cloud_.SaveProvisioningUrl(url, ProvisioningUrlSaveMode::kPreserveCredentials);
}

}  // namespace rodakos
