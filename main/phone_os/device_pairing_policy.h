#pragma once

#include <string>

namespace rodakos {

enum class DevicePairingStatus {
    kPending,
    kConfirmed,
    kExpired,
    kRejected,
    kUnknown,
};

inline DevicePairingStatus ClassifyDevicePairingStatus(const std::string& value) {
    if (value == "pending") {
        return DevicePairingStatus::kPending;
    }
    if (value == "confirmed" || value == "approved") {
        return DevicePairingStatus::kConfirmed;
    }
    if (value == "expired") {
        return DevicePairingStatus::kExpired;
    }
    if (value == "rejected" || value == "revoked") {
        return DevicePairingStatus::kRejected;
    }
    return DevicePairingStatus::kUnknown;
}

inline bool MayEnableDeviceCloud(DevicePairingStatus status) {
    return status == DevicePairingStatus::kConfirmed;
}

inline bool ShouldResetDevicePairingRequest(DevicePairingStatus status) {
    return status == DevicePairingStatus::kExpired ||
           status == DevicePairingStatus::kRejected;
}

enum class DeviceUnbindRecoveryAction {
    kAlreadyClean,
    kRequestServer,
    kFinishLocalCleanup,
};

inline DeviceUnbindRecoveryAction GetDeviceUnbindRecoveryAction(
    bool unbind_pending, bool server_acknowledged, bool has_access_token) {
    if (server_acknowledged || (unbind_pending && !has_access_token)) {
        return DeviceUnbindRecoveryAction::kFinishLocalCleanup;
    }
    if (unbind_pending || has_access_token) {
        return DeviceUnbindRecoveryAction::kRequestServer;
    }
    return DeviceUnbindRecoveryAction::kAlreadyClean;
}

}  // namespace rodakos
