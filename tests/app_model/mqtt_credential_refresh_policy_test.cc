#include "test_framework.h"

#include "phone_os/mqtt_credential_refresh_policy.h"

namespace {

using rodakos::DecideMqttCredentialRefreshAction;
using rodakos::MqttCredentialRefreshAction;
using rodakos::MqttCredentialRefreshState;

}  // namespace

RODAK_TEST("MQTT credential refresh failure keeps the current client") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = false;
    state.has_client = true;
    state.same_session_identity = false;
    state.outbox_empty = false;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kKeepCurrentClient);
}

RODAK_TEST("MQTT credential refresh without a client starts a connection") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = true;
    state.has_client = false;
    state.same_session_identity = false;
    state.outbox_empty = false;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kStartConnection);
}

RODAK_TEST("MQTT credential refresh restarts when session identity changes") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = true;
    state.has_client = true;
    state.same_session_identity = false;
    state.outbox_empty = true;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kRestart);
}

RODAK_TEST("MQTT credential refresh restarts with a nonempty outbox") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = true;
    state.has_client = true;
    state.same_session_identity = true;
    state.outbox_empty = false;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kRestart);
}

RODAK_TEST("MQTT credential refresh restarts an already reconnected client") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = true;
    state.has_client = true;
    state.client_connected = true;
    state.same_session_identity = true;
    state.outbox_empty = true;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kRestart);
}

RODAK_TEST("MQTT credential refresh applies matching empty sessions in place") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = true;
    state.has_client = true;
    state.same_session_identity = true;
    state.outbox_empty = true;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kApplyInPlace);
}
