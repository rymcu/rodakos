#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace rodakos {

// docs/qr-provisioning.md is the wire contract. Keep this parser free of ESP-IDF headers so
// the host test target can run the contract's test vectors.
inline constexpr size_t kSetupQrMaxPayloadBytes = 512;
inline constexpr size_t kSetupQrMaxSsidBytes = 32;
inline constexpr size_t kSetupQrMaxUrlBytes = 256;

enum class SetupQrKind {
    kRodak,     // WiFi and Device Cloud bootstrap URL.
    kWifiOnly,  // Standard `WIFI:` code; the bootstrap URL is unchanged.
};

enum class SetupQrError {
    kNone,
    kNotProvisioning,
    kVersionUnsupported,
    kSyntaxInvalid,
    kSsidInvalid,
    kAuthUnsupported,
    kPasswordInvalid,
    kUrlInvalid,
    kUrlInsecure,
    kUrlLegacy,
    // Reported by the save step, not by ParseSetupQr: a pinned server cannot be replaced by a QR code.
    kServerPinned,
};

struct SetupQrPayload {
    SetupQrKind kind = SetupQrKind::kRodak;
    std::string ssid;
    // Empty for an open network.
    std::string password;
    bool open_network = false;
    // Canonical `https://<host>[:<port>]/api/v1/aiot/devices/bootstrap`; empty for kWifiOnly.
    std::string bootstrap_url;
};

// On failure `payload` is cleared, so a partly parsed password is never left behind.
SetupQrError ParseSetupQr(std::string_view text, SetupQrPayload& payload);

// Stable error code for logs and UI mapping. It never contains payload data.
const char* SetupQrErrorCode(SetupQrError error);

}  // namespace rodakos
