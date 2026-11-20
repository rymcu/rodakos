#include "test_framework.h"

#include "apps/settings/device_cloud_page_model.h"

namespace {

using rodakos_settings::DeviceCloudAction;
using rodakos_settings::DeviceCloudPageInputs;
using rodakos_settings::DeviceCloudPageState;
using rodakos_settings::ResolveDeviceCloudPage;

DeviceCloudPageInputs Bound() {
    DeviceCloudPageInputs in;
    in.aiot_registered = true;
    in.aiot_activated = true;
    return in;
}

}  // namespace

RODAK_TEST("Device cloud page offers scan first, then LAN, for a new device") {
    DeviceCloudPageInputs in;
    in.qr_available = true;
    in.lan_available = true;
    const auto layout = ResolveDeviceCloudPage(in);
    RODAK_CHECK(layout.state == DeviceCloudPageState::kUnbound);
    RODAK_CHECK(layout.primary == DeviceCloudAction::kScanQr);
    RODAK_CHECK(layout.secondary == DeviceCloudAction::kLanOnboarding);
    RODAK_CHECK(layout.details_link);
}

RODAK_TEST("Device cloud page gives a single available guide the full row") {
    DeviceCloudPageInputs qr_only;
    qr_only.qr_available = true;
    auto layout = ResolveDeviceCloudPage(qr_only);
    RODAK_CHECK(layout.primary == DeviceCloudAction::kScanQr);
    RODAK_CHECK(layout.secondary == DeviceCloudAction::kNone);
    RODAK_CHECK(layout.details_link);

    DeviceCloudPageInputs lan_only;
    lan_only.lan_available = true;
    layout = ResolveDeviceCloudPage(lan_only);
    RODAK_CHECK(layout.primary == DeviceCloudAction::kLanOnboarding);
    RODAK_CHECK(layout.secondary == DeviceCloudAction::kNone);
}

RODAK_TEST("Device cloud page falls back to manual pairing when no guide applies") {
    // A USB-pinned, unbound device: no QR (pinned) and no LAN onboarding (pinned).
    const auto layout = ResolveDeviceCloudPage(DeviceCloudPageInputs{});
    RODAK_CHECK(layout.state == DeviceCloudPageState::kUnbound);
    RODAK_CHECK(layout.primary == DeviceCloudAction::kManualPairing);
    RODAK_CHECK(layout.secondary == DeviceCloudAction::kDetails);
    RODAK_CHECK_FALSE(layout.details_link);
}

RODAK_TEST("Device cloud page shows check and details while pairing") {
    DeviceCloudPageInputs in;
    in.pairing_pending = true;
    in.qr_available = true;
    const auto layout = ResolveDeviceCloudPage(in);
    RODAK_CHECK(layout.state == DeviceCloudPageState::kPairing);
    RODAK_CHECK(layout.primary == DeviceCloudAction::kCheckPairing);
    RODAK_CHECK(layout.secondary == DeviceCloudAction::kDetails);
}

RODAK_TEST("Device cloud page offers retry after a pairing or refresh failure") {
    DeviceCloudPageInputs pairing_error;
    pairing_error.pairing_pending = true;
    pairing_error.pairing_error = true;
    auto layout = ResolveDeviceCloudPage(pairing_error);
    RODAK_CHECK(layout.state == DeviceCloudPageState::kAttention);
    RODAK_CHECK(layout.primary == DeviceCloudAction::kRetry);

    DeviceCloudPageInputs refresh_failed;
    refresh_failed.refresh_failed = true;
    layout = ResolveDeviceCloudPage(refresh_failed);
    RODAK_CHECK(layout.state == DeviceCloudPageState::kAttention);
    RODAK_CHECK(layout.primary == DeviceCloudAction::kRetry);
}

RODAK_TEST("Device cloud page shows details and unbind for a healthy binding") {
    auto in = Bound();
    in.qr_available = true;
    in.lan_available = true;
    const auto layout = ResolveDeviceCloudPage(in);
    RODAK_CHECK(layout.state == DeviceCloudPageState::kBound);
    RODAK_CHECK(layout.primary == DeviceCloudAction::kDetails);
    RODAK_CHECK(layout.secondary == DeviceCloudAction::kUnbind);
}

RODAK_TEST("Device cloud page never puts unbind next to retry") {
    for (const bool refresh_failed : {true, false}) {
        auto in = Bound();
        in.refresh_failed = refresh_failed;
        in.diagnostic_needs_attention = !refresh_failed;
        const auto layout = ResolveDeviceCloudPage(in);
        RODAK_CHECK(layout.state == DeviceCloudPageState::kAttention);
        RODAK_CHECK(layout.primary == DeviceCloudAction::kRetry);
        RODAK_CHECK(layout.secondary == DeviceCloudAction::kDetails);
    }
}

RODAK_TEST("Device cloud page resumes an unfinished unbind instead of pairing") {
    auto in = Bound();
    in.unbind_pending = true;
    in.qr_available = true;
    in.pairing_pending = true;
    const auto layout = ResolveDeviceCloudPage(in);
    RODAK_CHECK(layout.state == DeviceCloudPageState::kUnbindPending);
    RODAK_CHECK(layout.primary == DeviceCloudAction::kContinueUnbind);
    RODAK_CHECK(layout.secondary == DeviceCloudAction::kDetails);

    DeviceCloudPageInputs unbound_marker;
    unbound_marker.unbind_pending = true;
    RODAK_CHECK(ResolveDeviceCloudPage(unbound_marker).state ==
                DeviceCloudPageState::kUnbindPending);
}

RODAK_TEST("Device cloud page routes a broken trust record to details only") {
    auto in = Bound();
    in.server_trust_error = true;
    in.unbind_pending = true;
    const auto layout = ResolveDeviceCloudPage(in);
    RODAK_CHECK(layout.state == DeviceCloudPageState::kTrustBroken);
    RODAK_CHECK(layout.primary == DeviceCloudAction::kDetails);
    RODAK_CHECK(layout.secondary == DeviceCloudAction::kNone);
}

RODAK_TEST("Device cloud page marks the primary busy during a request") {
    DeviceCloudPageInputs in;
    in.pairing_pending = true;
    in.refresh_in_progress = true;
    RODAK_CHECK(ResolveDeviceCloudPage(in).primary_busy);
    in.refresh_in_progress = false;
    RODAK_CHECK_FALSE(ResolveDeviceCloudPage(in).primary_busy);
}
