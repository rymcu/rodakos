#pragma once

#include <cstdint>
#include <atomic>

namespace rodakos {

enum class CloudDiagnosticCode {
    kReady,
    kUnconfigured,
    kRefreshing,
    kCredentialsRejected,
    kCredentialsExpired,
    kRefreshFailed,
    kNetworkUnavailable,
    kVoiceUnavailable,
    kTrustUnavailable,
    kCancelled,
};

struct CloudDiagnosticState {
    CloudDiagnosticCode code = CloudDiagnosticCode::kUnconfigured;
    int64_t updated_at_ms = 0;
    uint32_t revision = 0;
};

// Shared RAM ordering avoids ties within a millisecond across cloud and voice
// workers. This counter is neither persisted nor used as an identity/version.
inline uint32_t NextCloudDiagnosticRevision() {
    static std::atomic<uint32_t> revision{0};
    return revision.fetch_add(1, std::memory_order_relaxed) + 1;
}

inline bool IsNewerCloudDiagnostic(uint32_t candidate, uint32_t current) {
    return candidate != current && static_cast<int32_t>(candidate - current) > 0;
}

// Only fixed text crosses the device UI boundary; server response text, URLs
// and credentials are never used to construct a recovery diagnosis.
inline const char* CloudDiagnosticTitle(CloudDiagnosticCode code) {
    switch (code) {
        case CloudDiagnosticCode::kReady: return "Ready for wake";
        case CloudDiagnosticCode::kUnconfigured: return "Device Cloud not connected";
        case CloudDiagnosticCode::kRefreshing: return "Refreshing credentials";
        case CloudDiagnosticCode::kCredentialsRejected: return "Credentials rejected";
        case CloudDiagnosticCode::kCredentialsExpired: return "Credentials need refresh";
        case CloudDiagnosticCode::kRefreshFailed: return "Credential refresh failed";
        case CloudDiagnosticCode::kNetworkUnavailable: return "Server unreachable";
        case CloudDiagnosticCode::kVoiceUnavailable: return "Voice service unavailable";
        case CloudDiagnosticCode::kTrustUnavailable: return "Server trust needs attention";
        case CloudDiagnosticCode::kCancelled: return "Preparation cancelled";
    }
    return "Device Cloud needs attention";
}

inline const char* CloudDiagnosticHint(CloudDiagnosticCode code) {
    switch (code) {
        case CloudDiagnosticCode::kReady: return "Say the wake word to start.";
        case CloudDiagnosticCode::kUnconfigured: return "Open Device Cloud to connect Rodak.";
        case CloudDiagnosticCode::kRefreshing: return "Wait, or disable wake to stop voice.";
        case CloudDiagnosticCode::kCredentialsRejected: return "Check device access in Rodak, then retry.";
        case CloudDiagnosticCode::kCredentialsExpired: return "Wake again or refresh in Device Cloud.";
        case CloudDiagnosticCode::kRefreshFailed: return "Check Rodak, then retry Device Cloud.";
        case CloudDiagnosticCode::kNetworkUnavailable: return "Check WiFi and Rodak, then retry.";
        case CloudDiagnosticCode::kVoiceUnavailable: return "Enable voice in Rodak, then refresh.";
        case CloudDiagnosticCode::kTrustUnavailable: return "Check trusted USB setup; keep the binding.";
        case CloudDiagnosticCode::kCancelled: return "Wake again when ready.";
    }
    return "Open Settings > Device Cloud.";
}

}  // namespace rodakos
