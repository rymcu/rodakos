#include "phone_os/device_pairing_policy.h"
#include "test_framework.h"

RODAK_TEST("Device pairing only enables cloud after confirmation") {
    using rodakos::ClassifyDevicePairingStatus;
    using rodakos::MayEnableDeviceCloud;
    using rodakos::ShouldResetDevicePairingRequest;
    using rodakos::DeviceUnbindRecoveryAction;
    using rodakos::GetDeviceUnbindRecoveryAction;

    RODAK_CHECK(!MayEnableDeviceCloud(ClassifyDevicePairingStatus("pending")));
    RODAK_CHECK(MayEnableDeviceCloud(ClassifyDevicePairingStatus("confirmed")));
    RODAK_CHECK(!MayEnableDeviceCloud(ClassifyDevicePairingStatus("expired")));
    RODAK_CHECK(!MayEnableDeviceCloud(ClassifyDevicePairingStatus("rejected")));
    RODAK_CHECK(!MayEnableDeviceCloud(ClassifyDevicePairingStatus("unexpected")));

    RODAK_CHECK(!ShouldResetDevicePairingRequest(ClassifyDevicePairingStatus("pending")));
    RODAK_CHECK(!ShouldResetDevicePairingRequest(ClassifyDevicePairingStatus("confirmed")));
    RODAK_CHECK(ShouldResetDevicePairingRequest(ClassifyDevicePairingStatus("expired")));
    RODAK_CHECK(ShouldResetDevicePairingRequest(ClassifyDevicePairingStatus("rejected")));
    RODAK_CHECK(ShouldResetDevicePairingRequest(ClassifyDevicePairingStatus("revoked")));
    RODAK_CHECK_EQ(GetDeviceUnbindRecoveryAction(false, false, false),
                   DeviceUnbindRecoveryAction::kAlreadyClean);
    RODAK_CHECK_EQ(GetDeviceUnbindRecoveryAction(false, false, true),
                   DeviceUnbindRecoveryAction::kRequestServer);
    RODAK_CHECK_EQ(GetDeviceUnbindRecoveryAction(true, false, true),
                   DeviceUnbindRecoveryAction::kRequestServer);
    RODAK_CHECK_EQ(GetDeviceUnbindRecoveryAction(true, false, false),
                   DeviceUnbindRecoveryAction::kFinishLocalCleanup);
    RODAK_CHECK_EQ(GetDeviceUnbindRecoveryAction(true, true, true),
                   DeviceUnbindRecoveryAction::kFinishLocalCleanup);
}
