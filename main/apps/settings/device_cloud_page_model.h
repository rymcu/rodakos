#pragma once

#include <array>
#include <cstddef>

// Pure state -> layout decision for Settings > Connect Rodak. Free of LVGL and ESP-IDF so the
// host tests can cover every branch; settings_device_cloud_page.cc only renders the result.
namespace rodakos_settings {

enum class DeviceCloudPageState {
    kUnbound,
    kPairing,
    kBound,
    kAttention,
    kUnbindPending,
    // The server trust record cannot be read; only USB can repair it.
    kTrustBroken,
};

enum class DeviceCloudAction {
    kNone,
    kScanQr,
    kLanOnboarding,
    kManualPairing,
    kCheckPairing,
    kRetry,
    kDetails,
    kUnbind,
    kContinueUnbind,
};

struct DeviceCloudPageInputs {
    bool aiot_registered = false;
    bool aiot_activated = false;
    bool unbind_pending = false;
    bool server_trust_error = false;
    bool pairing_pending = false;
    bool pairing_error = false;
    bool refresh_failed = false;
    bool refresh_in_progress = false;
    // Bound, but the cloud diagnostic is neither Ready nor Refreshing.
    bool diagnostic_needs_attention = false;
    bool qr_available = false;
    bool lan_available = false;
};

struct DeviceCloudPageLayout {
    DeviceCloudPageState state = DeviceCloudPageState::kUnbound;
    // Left is the primary action. kNone on the right makes the left button span the full row.
    DeviceCloudAction primary = DeviceCloudAction::kNone;
    DeviceCloudAction secondary = DeviceCloudAction::kNone;
    // A details link in the status card header, for states whose action row has no Details.
    bool details_link = false;
    // Show the primary as busy (disabled) while a refresh or unbind request runs.
    bool primary_busy = false;
};

DeviceCloudPageLayout ResolveDeviceCloudPage(const DeviceCloudPageInputs& inputs);

}  // namespace rodakos_settings
