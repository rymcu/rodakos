#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "phone_os/device_cloud_config.h"

namespace rodakos {

// Keep the wire contract independent of ESP-IDF so the host test target can
// exercise it without hardware headers.
inline constexpr char kSerialProvisioningFramePrefix[] = "RODAK_PROVISION_V1 ";
inline constexpr size_t kSerialProvisioningFramePrefixBytes =
    sizeof(kSerialProvisioningFramePrefix) - 1;
inline constexpr size_t kSerialProvisioningMaxFrameBytes = 2048;
inline constexpr size_t kSerialProvisioningMaxJsonBytes =
    kSerialProvisioningMaxFrameBytes - kSerialProvisioningFramePrefixBytes - 1;
inline constexpr size_t kSerialProvisioningMaxBootstrapUrlBytes = 256;

static_assert(kSerialProvisioningMaxFrameBytes >
                  kSerialProvisioningFramePrefixBytes + 1,
              "provisioning frame limit must leave room for a payload and LF");

enum class SerialProvisioningFrameResult {
    kNeedMore,
    kLineReady,
    kFrameTooLarge,
};

// A terminal CR is omitted for CRLF compatibility, but embedded control bytes
// remain visible to the validator instead of being silently rewritten.
class SerialProvisioningFrameAccumulator {
public:
    SerialProvisioningFrameAccumulator();

    SerialProvisioningFrameAccumulator(const SerialProvisioningFrameAccumulator&) = delete;
    SerialProvisioningFrameAccumulator& operator=(
        const SerialProvisioningFrameAccumulator&) = delete;

    SerialProvisioningFrameResult Push(uint8_t byte, std::string& line);
    void Reset();

    size_t pending_wire_bytes() const { return pending_wire_bytes_; }

private:
    std::string line_;
    size_t pending_wire_bytes_ = 0;
    bool oversized_ = false;
};

bool IsValidSerialProvisioningBootstrapUrl(const std::string& url);

// Return the canonical effective bootstrap URL. Origins and the canonical
// bootstrap path resolve to the same value; host names are case-insensitive
// and default HTTP(S) ports are omitted.
bool NormalizeSerialProvisioningBootstrapUrl(const std::string& url,
                                             std::string& normalized);

// cJSON stores decoded strings as NUL-terminated buffers. This helper rejects
// literal or JSON-escaped NUL bytes before parsing can truncate a field.
bool ContainsSerialProvisioningJsonNul(const std::string& json);

constexpr bool ShouldClearSerialProvisioningPendingAfterCloudSaveFailure(
    bool wifi_restored, ProvisioningUrlSaveResult cloud_save_result) {
    return wifi_restored &&
           cloud_save_result == ProvisioningUrlSaveResult::kFailedRolledBack;
}

}  // namespace rodakos
