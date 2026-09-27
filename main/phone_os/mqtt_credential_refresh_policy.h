#pragma once

#include <cstdint>

namespace rodakos {

enum class MqttCredentialRefreshAction {
    kKeepCurrentClient,
    kStartConnection,
    kRestart,
    kApplyInPlace,
};

struct MqttCredentialRefreshState {
    bool refresh_succeeded = false;
    bool has_client = false;
    bool client_connected = false;
    bool same_session_identity = false;
    bool outbox_empty = false;
};

MqttCredentialRefreshAction DecideMqttCredentialRefreshAction(
    const MqttCredentialRefreshState& state);

enum class MqttTransportRecoveryAction {
    kWait,
    kDeferWhileVoiceActive,
    kRefresh,
};

class MqttTransportRecoveryPolicy {
public:
    void RecordTransportFailure();
    void MarkConnected();
    void MarkRefreshStarted(int64_t now_ms);
    MqttTransportRecoveryAction Decide(int64_t now_ms, bool refresh_pending,
                                       bool voice_active) const;

private:
    uint32_t consecutive_failures_ = 0;
    int64_t next_refresh_allowed_ms_ = 0;
};

}  // namespace rodakos
