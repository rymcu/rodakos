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
    kDeviceMemoryLow,
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
        case CloudDiagnosticCode::kDeviceMemoryLow: return "Device memory low";
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
        case CloudDiagnosticCode::kDeviceMemoryLow: return "Close other apps or restart, then retry.";
    }
    return "Open Settings > Device Cloud.";
}

// Chinese user-facing text, shared by Settings > 连接服务 and the Assistant app so both pages
// say the same thing. Like the English text above, it never includes server or credential text.
inline const char* CloudDiagnosticTitleZh(CloudDiagnosticCode code) {
    switch (code) {
        case CloudDiagnosticCode::kReady: return "已连接";
        case CloudDiagnosticCode::kUnconfigured: return "尚未连接";
        case CloudDiagnosticCode::kRefreshing: return "正在更新凭据";
        case CloudDiagnosticCode::kCredentialsRejected: return "设备凭据被拒绝";
        case CloudDiagnosticCode::kCredentialsExpired: return "设备凭据需要更新";
        case CloudDiagnosticCode::kRefreshFailed: return "凭据更新失败";
        case CloudDiagnosticCode::kNetworkUnavailable: return "无法连接服务器";
        case CloudDiagnosticCode::kVoiceUnavailable: return "语音服务不可用";
        case CloudDiagnosticCode::kTrustUnavailable: return "服务器证书需要处理";
        case CloudDiagnosticCode::kCancelled: return "连接已取消";
        case CloudDiagnosticCode::kDeviceMemoryLow: return "设备内存不足";
    }
    return "连接需要处理";
}

inline const char* CloudDiagnosticHintZh(CloudDiagnosticCode code) {
    switch (code) {
        case CloudDiagnosticCode::kCredentialsRejected: return "请在服务端确认设备权限，然后点「重试」。";
        case CloudDiagnosticCode::kCredentialsExpired:
        case CloudDiagnosticCode::kRefreshFailed: return "请确认服务端在运行，然后点「重试」。";
        case CloudDiagnosticCode::kNetworkUnavailable: return "请检查 Wi-Fi 和服务器地址，然后点「重试」。";
        case CloudDiagnosticCode::kVoiceUnavailable: return "请在服务端启用语音，然后点「重试」。";
        case CloudDiagnosticCode::kTrustUnavailable: return "服务器证书与固定的不一致。绑定会保留，请检查 USB 下发的服务器信任。";
        case CloudDiagnosticCode::kDeviceMemoryLow: return "请关闭其他应用或重启设备后重试。";
        default: break;
    }
    return "请点「重试」。";
}

}  // namespace rodakos
