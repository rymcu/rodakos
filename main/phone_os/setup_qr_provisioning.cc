#include "phone_os/setup_qr_provisioning.h"

#include "phone_os/serial_provisioning_protocol.h"

namespace rodakos {
namespace {

bool RestoreWifi(SetupQrProvisioningStore& store, bool had_previous, const std::string& ssid,
                 const std::string& password) {
    return had_previous ? store.SaveWifi(ssid, password) : store.ClearWifi();
}

}  // namespace

SetupQrApplyStatus ApplySetupQrProvisioning(const SetupQrPayload& payload,
                                            SetupQrProvisioningStore& store) {
    const bool changes_server = payload.kind == SetupQrKind::kRodak;
    if (changes_server && store.ServerTrust() != SetupQrServerTrust::kNone) {
        return SetupQrApplyStatus::kServerPinned;
    }

    std::string previous_ssid;
    std::string previous_password;
    const bool had_previous_wifi = store.LoadWifi(previous_ssid, previous_password);

    if (!store.SetTransactionPending(true)) return SetupQrApplyStatus::kStorageFailed;

    if (!store.SaveWifi(payload.ssid, payload.password)) {
        if (!RestoreWifi(store, had_previous_wifi, previous_ssid, previous_password)) {
            return SetupQrApplyStatus::kStateUncertain;
        }
        return store.SetTransactionPending(false) ? SetupQrApplyStatus::kStorageFailed
                                                  : SetupQrApplyStatus::kStateUncertain;
    }

    SetupQrApplyStatus status = SetupQrApplyStatus::kWifiSaved;
    if (changes_server) {
        const ProvisioningUrlSaveResult saved = store.SaveBootstrapUrl(payload.bootstrap_url);
        if (saved == ProvisioningUrlSaveResult::kSaved) {
            status = SetupQrApplyStatus::kServerChanged;
        } else if (saved == ProvisioningUrlSaveResult::kUnchanged) {
            status = SetupQrApplyStatus::kServerUnchanged;
        } else {
            const bool wifi_restored =
                RestoreWifi(store, had_previous_wifi, previous_ssid, previous_password);
            if (!ShouldClearSerialProvisioningPendingAfterCloudSaveFailure(wifi_restored, saved)) {
                return SetupQrApplyStatus::kStateUncertain;
            }
            return store.SetTransactionPending(false) ? SetupQrApplyStatus::kStorageFailed
                                                      : SetupQrApplyStatus::kStateUncertain;
        }
    }

    // Both values are durable; a reset from here on must not trigger recovery that clears them.
    if (!store.SetTransactionPending(false)) return SetupQrApplyStatus::kStateUncertain;
    return status;
}

const char* SetupQrApplyStatusCode(SetupQrApplyStatus status) {
    switch (status) {
        case SetupQrApplyStatus::kWifiSaved: return "qr_wifi_saved";
        case SetupQrApplyStatus::kServerUnchanged: return "qr_server_unchanged";
        case SetupQrApplyStatus::kServerChanged: return "qr_server_changed";
        case SetupQrApplyStatus::kServerPinned: return SetupQrErrorCode(SetupQrError::kServerPinned);
        case SetupQrApplyStatus::kStorageFailed: return "qr_storage_failed";
        case SetupQrApplyStatus::kStateUncertain: return "qr_state_uncertain";
    }
    return "qr_unknown";
}

}  // namespace rodakos
