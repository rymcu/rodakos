#include "test_framework.h"

#include "phone_os/mqtt_credential_refresh_policy.h"

namespace {

using rodakos::DecideMqttCredentialRefreshAction;
using rodakos::MqttCredentialRefreshAction;
using rodakos::MqttCredentialRefreshState;
using rodakos::MqttTransportRecoveryAction;
using rodakos::MqttTransportRecoveryPolicy;

}  // namespace

RODAK_TEST("MQTT credential refresh failure keeps the current client") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = false;
    state.has_client = true;
    state.same_effect_authority = false;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kKeepCurrentClient);
}

RODAK_TEST("MQTT credential refresh without a client starts a connection") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = true;
    state.has_client = false;
    state.same_effect_authority = false;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kStartConnection);
}

RODAK_TEST("MQTT credential refresh isolates a changed effect authority with restart") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = true;
    state.has_client = true;
    state.same_effect_authority = false;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kRestart);
}

RODAK_TEST("MQTT credential refresh replaces every existing same authority client") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = true;
    state.has_client = true;
    state.same_effect_authority = true;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kReplaceClient);
}

RODAK_TEST("MQTT failed same authority refresh never replaces the client") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = false;
    state.has_client = true;
    state.same_effect_authority = true;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kKeepCurrentClient);
}

RODAK_TEST("MQTT failed refresh without a client does not start a connection") {
    MqttCredentialRefreshState state;
    state.refresh_succeeded = false;
    state.has_client = false;
    state.same_effect_authority = true;

    RODAK_CHECK_EQ(DecideMqttCredentialRefreshAction(state),
                   MqttCredentialRefreshAction::kKeepCurrentClient);
}

RODAK_TEST("MQTT transport recovery waits for three consecutive TCP failures") {
    MqttTransportRecoveryPolicy policy;
    RODAK_CHECK_EQ(policy.Decide(0, false, false), MqttTransportRecoveryAction::kWait);
    policy.RecordTransportFailure();
    policy.RecordTransportFailure();
    RODAK_CHECK_EQ(policy.Decide(12000, false, false), MqttTransportRecoveryAction::kWait);
    policy.RecordTransportFailure();
    RODAK_CHECK_EQ(policy.Decide(24000, false, false), MqttTransportRecoveryAction::kRefresh);
}

RODAK_TEST("MQTT transport recovery coalesces with an existing credential refresh") {
    MqttTransportRecoveryPolicy policy;
    for (int i = 0; i < 100; ++i) {
        policy.RecordTransportFailure();
    }
    RODAK_CHECK_EQ(policy.Decide(24000, true, false), MqttTransportRecoveryAction::kWait);
    policy.MarkRefreshStarted(24000);
    RODAK_CHECK_EQ(policy.Decide(100000, false, false), MqttTransportRecoveryAction::kWait);
}

RODAK_TEST("MQTT transport recovery preserves work while a voice session is active") {
    MqttTransportRecoveryPolicy policy;
    for (int i = 0; i < 3; ++i) {
        policy.RecordTransportFailure();
    }
    RODAK_CHECK_EQ(policy.Decide(24000, false, true),
                   MqttTransportRecoveryAction::kDeferWhileVoiceActive);
    RODAK_CHECK_EQ(policy.Decide(120000, false, true),
                   MqttTransportRecoveryAction::kDeferWhileVoiceActive);
    RODAK_CHECK_EQ(policy.Decide(120001, false, false), MqttTransportRecoveryAction::kRefresh);
}

RODAK_TEST("MQTT failed refresh attempts observe the full sixty second cooldown") {
    MqttTransportRecoveryPolicy policy;
    policy.MarkRefreshStarted(25000);
    for (int i = 0; i < 100; ++i) {
        policy.RecordTransportFailure();
    }
    RODAK_CHECK_EQ(policy.Decide(84999, false, false), MqttTransportRecoveryAction::kWait);
    RODAK_CHECK_EQ(policy.Decide(85000, false, false), MqttTransportRecoveryAction::kRefresh);
    policy.MarkRefreshStarted(85000);
    for (int i = 0; i < 3; ++i) {
        policy.RecordTransportFailure();
    }
    RODAK_CHECK_EQ(policy.Decide(144999, false, false), MqttTransportRecoveryAction::kWait);
    RODAK_CHECK_EQ(policy.Decide(145000, false, false), MqttTransportRecoveryAction::kRefresh);
}

RODAK_TEST("MQTT reconnect clears old failures and retains the refresh cooldown") {
    MqttTransportRecoveryPolicy policy;
    for (int i = 0; i < 3; ++i) {
        policy.RecordTransportFailure();
    }
    policy.MarkConnected();
    RODAK_CHECK_EQ(policy.Decide(24000, false, false), MqttTransportRecoveryAction::kWait);
    policy.RecordTransportFailure();
    policy.RecordTransportFailure();
    RODAK_CHECK_EQ(policy.Decide(30000, false, false), MqttTransportRecoveryAction::kWait);
    policy.RecordTransportFailure();
    policy.MarkRefreshStarted(30000);
    policy.MarkConnected();
    for (int i = 0; i < 3; ++i) {
        policy.RecordTransportFailure();
    }
    RODAK_CHECK_EQ(policy.Decide(89999, false, false), MqttTransportRecoveryAction::kWait);
    RODAK_CHECK_EQ(policy.Decide(90000, false, false), MqttTransportRecoveryAction::kRefresh);
}
