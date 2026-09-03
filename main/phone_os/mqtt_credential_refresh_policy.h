#pragma once

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

}  // namespace rodakos
