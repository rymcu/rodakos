#include "apps/settings/device_cloud_page_model.h"

namespace rodakos_settings {

DeviceCloudPageLayout ResolveDeviceCloudPage(const DeviceCloudPageInputs& in) {
    DeviceCloudPageLayout layout;
    layout.primary_busy = in.refresh_in_progress;

    if (in.server_trust_error) {
        layout.state = DeviceCloudPageState::kTrustBroken;
        layout.primary = DeviceCloudAction::kDetails;
        return layout;
    }
    // Checked before "bound": an unfinished unbind must never offer a new pairing.
    if (in.unbind_pending) {
        layout.state = DeviceCloudPageState::kUnbindPending;
        layout.primary = DeviceCloudAction::kContinueUnbind;
        layout.secondary = DeviceCloudAction::kDetails;
        return layout;
    }
    if (in.aiot_registered && in.aiot_activated) {
        if (in.refresh_failed || in.diagnostic_needs_attention) {
            // Unbind stays in Details so it never sits next to Retry.
            layout.state = DeviceCloudPageState::kAttention;
            layout.primary = DeviceCloudAction::kRetry;
            layout.secondary = DeviceCloudAction::kDetails;
        } else {
            layout.state = DeviceCloudPageState::kBound;
            layout.primary = DeviceCloudAction::kDetails;
            layout.secondary = DeviceCloudAction::kUnbind;
        }
        return layout;
    }
    if (in.pairing_pending && !in.pairing_error && !in.refresh_failed) {
        layout.state = DeviceCloudPageState::kPairing;
        layout.primary = DeviceCloudAction::kCheckPairing;
        layout.secondary = DeviceCloudAction::kDetails;
        return layout;
    }
    if (in.pairing_error || in.refresh_failed) {
        layout.state = DeviceCloudPageState::kAttention;
        layout.primary = DeviceCloudAction::kRetry;
        layout.secondary = DeviceCloudAction::kDetails;
        return layout;
    }

    layout.state = DeviceCloudPageState::kUnbound;
    if (in.qr_available && in.lan_available) {
        layout.primary = DeviceCloudAction::kScanQr;
        layout.secondary = DeviceCloudAction::kLanOnboarding;
        layout.details_link = true;
    } else if (in.qr_available) {
        layout.primary = DeviceCloudAction::kScanQr;
        layout.details_link = true;
    } else if (in.lan_available) {
        layout.primary = DeviceCloudAction::kLanOnboarding;
        layout.details_link = true;
    } else {
        // Neither guide applies (for example a USB-pinned server): pair against the saved URL.
        layout.primary = DeviceCloudAction::kManualPairing;
        layout.secondary = DeviceCloudAction::kDetails;
    }
    return layout;
}

}  // namespace rodakos_settings
