#include "phone_os/mqtt_credential_refresh_policy.h"

namespace rodakos {

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

}  // namespace rodakos
