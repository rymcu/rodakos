#include "phone_os/mqtt_credential_refresh_policy.h"

namespace rodakos {
namespace {
constexpr uint32_t kTransportFailureThreshold = 3;
constexpr int64_t kTransportRefreshCooldownMs = 60 * 1000;
}  // namespace

MqttCredentialRefreshAction DecideMqttCredentialRefreshAction(
    const MqttCredentialRefreshState& state) {
    if (!state.refresh_succeeded) {
        return MqttCredentialRefreshAction::kKeepCurrentClient;
    }
    if (!state.has_client) {
        return MqttCredentialRefreshAction::kStartConnection;
    }
    if (state.client_connected || !state.same_session_identity || !state.outbox_empty) {
        return MqttCredentialRefreshAction::kRestart;
    }
    return MqttCredentialRefreshAction::kApplyInPlace;
}

void MqttTransportRecoveryPolicy::RecordTransportFailure() {
    if (consecutive_failures_ < kTransportFailureThreshold) {
        ++consecutive_failures_;
    }
}

void MqttTransportRecoveryPolicy::MarkConnected() {
    consecutive_failures_ = 0;
}

void MqttTransportRecoveryPolicy::MarkRefreshStarted(int64_t now_ms) {
    consecutive_failures_ = 0;
    next_refresh_allowed_ms_ = now_ms + kTransportRefreshCooldownMs;
}

MqttTransportRecoveryAction MqttTransportRecoveryPolicy::Decide(
    int64_t now_ms, bool refresh_pending, bool voice_active) const {
    if (consecutive_failures_ < kTransportFailureThreshold || refresh_pending ||
        now_ms < next_refresh_allowed_ms_) {
        return MqttTransportRecoveryAction::kWait;
    }
    return voice_active ? MqttTransportRecoveryAction::kDeferWhileVoiceActive
                        : MqttTransportRecoveryAction::kRefresh;
}

}  // namespace rodakos
