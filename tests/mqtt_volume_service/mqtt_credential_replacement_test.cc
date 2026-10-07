#include "host_runtime.h"
#include "phone_os/battery_monitor.h"
#include "phone_os/light_service.h"
#include "phone_os/mqtt_credential_refresh_policy.h"
#include "phone_os/mqtt_volume_effect.h"
#include "phone_os/mqtt_light_effect.h"
#include "phone_os/mqtt_command_ledger.h"
#include "phone_os/stream_lease.h"
#include <array>
#include <memory>
#include <utility>
// Read cancellation admission only; all transitions still enter the public service/SDK APIs.
#define private public
#include "phone_os/unified_mqtt_service.h"
#undef private
#include "service_fixture.h"
#include "phone_os/voice_wake_service.h"
#include "phone_os/webrtc_display_service.h"

#include <chrono>
#include <condition_variable>
#include <future>

using namespace mqtt_host;

namespace {
class Gate {
public:
    ~Gate() { Release(); }
    bool Enter() {
        std::unique_lock<std::mutex> lock(mutex_);
        entered_ = true;
        changed_.notify_all();
        return changed_.wait_for(lock, std::chrono::seconds(6), [&]() { return released_; });
    }
    bool Wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(4), [&]() { return entered_; });
    }
    void Release() {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_ = false;
    bool released_ = false;
};

struct ResetHooks {
    ~ResetHooks() { SetRefreshHook({}); SetSdkHook({}); }
};

void Rotate(const std::string& credential) {
    auto config = Config();
    config.mqtt_password = credential;
    SetConfig(config);
}

bool WaitReplacement(Fixture& fixture, esp_mqtt_client_handle_t original) {
    return WaitUntil([&]() {
        return Restarts() != 0 ||
            (CurrentClient() != nullptr && CurrentClient() != original && fixture.service.IsConnected()) ||
            (CurrentClient() == original && CredentialRevision() > 0 && fixture.service.IsConnected());
    });
}

void CheckReplacement(Fixture& fixture, esp_mqtt_client_handle_t original,
                      const std::string& credential) {
    RODAK_CHECK(WaitReplacement(fixture, original));
    RODAK_CHECK_EQ(Restarts(), 0u);
    RODAK_CHECK(CurrentClient() != original);
    const auto clients = ClientSnapshots();
    RODAK_CHECK(clients.size() >= 2);
    RODAK_CHECK(clients[clients.size() - 2].destroyed);
    RODAK_CHECK(clients[clients.size() - 2].exited);
    RODAK_CHECK_EQ(clients.back().credential, credential);
    RODAK_CHECK(clients.back().connected);
    const auto events = LifecycleEvents();
    size_t old_destroy = events.size(), next_init = events.size();
    for (size_t index = 0; index < events.size(); ++index) {
        if (events[index].client_id == clients[clients.size() - 2].id &&
            events[index].action == "destroy") old_destroy = index;
        if (events[index].client_id == clients.back().id &&
            events[index].action == "init") next_init = index;
    }
    RODAK_CHECK(old_destroy < next_init);
}

void CheckRetry(SdkOperation operation, const char* failure_event) {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    Rotate("test-token-2");
    FailNextSdk(operation);
    Disconnect();
    RejectCredentials();
    CheckReplacement(f, original, "test-token-2");
    f.Barrier();
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
    unsigned failures = 0;
    for (const auto& event : LifecycleEvents())
        if (event.action == failure_event) ++failures;
    RODAK_CHECK_EQ(failures, 1u);
    const auto clients = ClientSnapshots();
    for (size_t index = 0; index + 1 < clients.size(); ++index)
        RODAK_CHECK(clients[index].destroyed);
}
}

RODAK_TEST("credential replacement destroys the same-authority empty client before fresh init") {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    CheckReplacement(f, original, "test-token-2");
    f.Barrier();
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
}

RODAK_TEST("credential replacement coalesces old HTTP-time rejection and preserves a new-client rejection") {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    Gate http;
    ResetHooks reset;
    std::atomic<bool> gate_completed{false};
    SetRefreshHook([&](unsigned call, rodakos::DeviceCloudConfig&) {
        if (call != 2) return true;
        gate_completed = http.Enter();
        return gate_completed.load();
    });
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    const bool entered = http.Wait();
    RejectCredentialsOn(original);
    Connect();
    http.Release();
    RODAK_CHECK(entered);
    CheckReplacement(f, original, "test-token-2");
    f.Barrier();
    RODAK_CHECK(gate_completed);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);

    auto* replacement = CurrentClient();
    Rotate("test-token-3");
    Disconnect();
    RejectCredentials();
    CheckReplacement(f, replacement, "test-token-3");
    f.Barrier();
    RODAK_CHECK_EQ(RefreshCalls(), 3u);
}

RODAK_TEST("credential replacement discards the SDK outbox and old queued receipts on the wire") {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    HoldWire(true);
    RODAK_CHECK(f.service.Publish("devices/test-device/telemetry", "old-outbox-sentinel"));
    f.Send(Request());
    RODAK_CHECK(WaitUntil([]() { return ReceiptCount() == 1; }));
    HoldUserEvents(true);
    Message("devices/test-device/commands/old-ping", "ping");
    RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    CheckReplacement(f, original, "test-token-2");
    HoldWire(false);
    HoldUserEvents(false);
    f.Barrier();
    for (const auto& item : WirePublications()) {
        RODAK_CHECK(item.payload != "old-outbox-sentinel");
        RODAK_CHECK(item.topic != "devices/test-device/commands/old-ping/ack");
        RODAK_CHECK(item.topic != "devices/test-device/effects/receipt");
    }
    const auto old = ClientSnapshots().front();
    RODAK_CHECK_EQ(old.outbox_count, 0u);
    RODAK_CHECK_EQ(old.custom_event_count, 0u);
    f.Send(Request());
    auto replay = Parse(f.Receipt(1));
    RODAK_CHECK_EQ(Get(Get(replay.get(), "receipt"), "configurationRevision")->valueint, 1);
    RODAK_CHECK_EQ(f.output.volume(), 30);
}

RODAK_TEST("credential replacement preserves new-client rejection received before SDK start returns") {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    ResetHooks reset;
    std::atomic<bool> rejected{false};
    SetSdkHook([&](SdkOperation operation, esp_mqtt_client_handle_t client) {
        if (operation == SdkOperation::kStart && client != original && !rejected.exchange(true)) {
            Rotate("test-token-3");
            RejectCredentialsOn(client);
        }
    });
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    RODAK_CHECK(WaitUntil([&]() {
        const auto clients = ClientSnapshots();
        return clients.size() == 3 && clients.back().connected && f.service.IsConnected();
    }));
    RODAK_CHECK(rejected);
    CheckReplacement(f, original, "test-token-3");
    f.Barrier();
    RODAK_CHECK_EQ(RefreshCalls(), 3u);
}

RODAK_TEST("credential replacement cancels its snapshot when an external Stop wins during HTTP") {
    Fixture f;
    f.Start();
    Gate http;
    ResetHooks reset;
    SetRefreshHook([&](unsigned call, rodakos::DeviceCloudConfig&) {
        return call != 2 || http.Enter();
    });
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    const bool entered = http.Wait();
    auto stop = std::async(std::launch::async, [&]() { f.service.Stop(); });
    const bool stopped_admission = WaitUntil([&]() {
        const auto clients = ClientSnapshots();
        return !clients.empty() && clients.front().destroyed;
    });
    http.Release();
    const bool completed = stop.wait_for(std::chrono::seconds(4)) == std::future_status::ready;
    if (completed) stop.get();
    RODAK_CHECK(entered);
    RODAK_CHECK(stopped_admission);
    RODAK_CHECK(completed);
    RODAK_CHECK_EQ(ClientSnapshots().size(), 1u);
    RODAK_CHECK(ClientSnapshots().front().destroyed);
    RODAK_CHECK_EQ(Restarts(), 0u);
}

RODAK_TEST("credential replacement retains the refreshed snapshot while voice is active") {
    rodakos::VoiceWakeService voice;
    Fixture f;
    f.service.SetVoiceWakeService(&voice);
    f.Start();
    auto* original = CurrentClient();
    Gate http;
    ResetHooks reset;
    SetRefreshHook([&](unsigned call, rodakos::DeviceCloudConfig&) {
        return call != 2 || http.Enter();
    });
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    const bool entered = http.Wait();
    auto state = voice.GetState();
    state.status = rodakos::VoiceWakeStatus::kAssistantActive;
    voice.SetState(state);
    http.Release();
    Connect();
    f.Barrier();
    RODAK_CHECK(entered);
    RODAK_CHECK(CurrentClient() == original);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
    RODAK_CHECK_EQ(Restarts(), 0u);
    state.status = rodakos::VoiceWakeStatus::kDisabled;
    voice.SetState(state);
    CheckReplacement(f, original, "test-token-2");
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
    f.service.SetVoiceWakeService(nullptr);
}

RODAK_TEST("credential replacement retries SDK init failure without rotating HTTP credentials again") {
    CheckRetry(SdkOperation::kInit, "init-failed");
}
RODAK_TEST("credential replacement retries SDK registration failure without retaining a partial client") {
    CheckRetry(SdkOperation::kRegister, "register-failed");
}
RODAK_TEST("credential replacement retries SDK start failure without retaining a partial client") {
    CheckRetry(SdkOperation::kStart, "start-failed");
}

RODAK_TEST("credential replacement rejects synchronous old SDK callbacks during retirement") {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    ResetHooks reset;
    std::atomic<bool> callback_returned{false};
    const auto old_queue_sequence = LastQueuedMessage();
    SetSdkHook([&](SdkOperation operation, esp_mqtt_client_handle_t client) {
        if (operation != SdkOperation::kStop || client != original) return;
        esp_mqtt_event_t connected;
        connected.event_id = MQTT_EVENT_CONNECTED;
        DeliverTo(client, connected);
        RejectCredentialsOn(client);
        const std::string topic = Config().mqtt_topic_shadow_desired;
        const std::string payload = Request("stale-during-retire", 9, 9);
        esp_mqtt_event_t message;
        message.event_id = MQTT_EVENT_DATA;
        message.topic = topic.c_str();
        message.topic_len = topic.size();
        message.data = payload.c_str();
        message.data_len = message.total_data_len = payload.size();
        DeliverTo(client, message);
        callback_returned = true;
    });
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    CheckReplacement(f, original, "test-token-2");
    RODAK_CHECK(callback_returned);
    RODAK_CHECK_EQ(LastQueuedMessage(), old_queue_sequence);
    f.Barrier();
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
    RODAK_CHECK_EQ(f.output.volume(), 60);
}

RODAK_TEST("credential replacement cancels a reliable PUBACK waiter without replaying its outbox") {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    HoldWire(true);
    auto publish = std::async(std::launch::async, [&]() {
        return f.ota.EmitProgress("old-reliable-sentinel", true);
    });
    const bool enqueued = WaitUntil([]() {
        for (const auto& item : QueuedPublications())
            if (item.payload == "old-reliable-sentinel") return true;
        return false;
    });
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    CheckReplacement(f, original, "test-token-2");
    const bool cancelled = publish.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    bool accepted = true;
    if (cancelled) accepted = publish.get();
    RODAK_CHECK(enqueued);
    RODAK_CHECK(cancelled);
    RODAK_CHECK_FALSE(accepted);
    HoldWire(false);
    for (const auto& item : WirePublications())
        RODAK_CHECK(item.payload != "old-reliable-sentinel");
}

RODAK_TEST("credential replacement and external Stop settle concurrent peer cleanup without lifecycle inversion") {
    rodakos::WebRtcDisplayService peer;
    Fixture f;
    peer.start_result = true;
    peer.terminal_on_stop_thread = true;
    f.service.SetWebRtcDisplayService(&peer);
    f.Start();
    Message("devices/test-device/commands/stream-start", "{\"command\":\"display.stream.start\",\"sessionId\":\"old-peer\"}");
    RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
    RODAK_CHECK(peer.running);
    const auto old_callbacks = peer.SavedCallbacks();
    Gate http;
    Gate cleanup;
    ResetHooks reset;
    SetRefreshHook([&](unsigned call, rodakos::DeviceCloudConfig&) { return call != 2 || http.Enter(); });
    Rotate("test-token-2");
    RejectCredentials();
    const bool http_entered = http.Wait();
    peer.before_stop_return = [&]() { (void)cleanup.Enter(); };
    http.Release();
    const bool cleanup_entered = cleanup.Wait();
    auto stop = std::async(std::launch::async, [&]() { f.service.Stop(); });
    const bool stop_admitted = WaitUntil([&]() { return !f.service.started_.load(); });
    // This old callback must return even while Stop is waiting for the worker.
    old_callbacks.state(ESP_PEER_STATE_CLOSED);
    cleanup.Release();
    const bool completed = stop.wait_for(std::chrono::seconds(4)) == std::future_status::ready;
    if (completed) stop.get();
    peer.before_stop_return = {};
    RODAK_CHECK(http_entered);
    RODAK_CHECK(cleanup_entered);
    RODAK_CHECK(stop_admitted);
    RODAK_CHECK(completed);
    RODAK_CHECK_FALSE(peer.running);
    RODAK_CHECK(peer.terminal_callback_returns > 0);
    RODAK_CHECK_EQ(Restarts(), 0u);
    RODAK_CHECK_EQ(ClientSnapshots().size(), 1u);
    RODAK_CHECK(ClientSnapshots().front().destroyed);
}

RODAK_TEST("credential replacement quarantines a stop failure until the old SDK has really exited") {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    Gate http;
    Gate sdk_exit;
    ResetHooks reset;
    SetRefreshHook([&](unsigned call, rodakos::DeviceCloudConfig&) { return call != 2 || http.Enter(); });
    SetSdkHook([&](SdkOperation operation, esp_mqtt_client_handle_t client) {
        if (operation == SdkOperation::kTaskExit && client == original) (void)sdk_exit.Enter();
    });
    Rotate("test-token-2");
    RejectCredentials();
    const bool http_entered = http.Wait();
    // Actual SDK has a run=false interval before outbox cleanup and STOPPED_BIT.
    BeginSdkExit(original);
    const bool exit_entered = sdk_exit.Wait();
    http.Release();
    const bool failed_safe = WaitUntil([]() { return Restarts() != 0; });
    const auto quarantined = ClientSnapshots();
    const auto before_callback = LastQueuedMessage();
    RejectCredentialsOn(original);
    esp_mqtt_event_t connected;
    connected.event_id = MQTT_EVENT_CONNECTED;
    DeliverTo(original, connected);
    const bool remained_offline = !f.service.IsConnected();
    const auto after_callback = LastQueuedMessage();
    sdk_exit.Release();
    const bool exited = WaitUntil([]() { return ClientSnapshots().front().exited; });
    if (exited) JoinExitedSdkForCleanup(original);
    RODAK_CHECK(http_entered);
    RODAK_CHECK(exit_entered);
    RODAK_CHECK(failed_safe);
    RODAK_CHECK_EQ(quarantined.size(), 1u);
    RODAK_CHECK_FALSE(quarantined.front().destroyed);
    RODAK_CHECK_FALSE(quarantined.front().exited);
    RODAK_CHECK(remained_offline);
    RODAK_CHECK_EQ(before_callback, after_callback);
    RODAK_CHECK(exited);
    for (const auto& event : LifecycleEvents())
        RODAK_CHECK(event.action != "unsafe-destroy-attempt");
}

RODAK_TEST("credential replacement retains a successfully started SDK whose task has not entered run") {
    Fixture f;
    Gate sdk_enter;
    ResetHooks reset;
    SetSdkHook([&](SdkOperation operation, esp_mqtt_client_handle_t) {
        if (operation == SdkOperation::kTaskEnter) (void)sdk_enter.Enter();
    });
    RODAK_CHECK(f.service.Start());
    const bool entered = sdk_enter.Wait();
    auto* client = CurrentClient();
    auto stop = std::async(std::launch::async, [&]() { f.service.Stop(); });
    const bool completed = stop.wait_for(std::chrono::seconds(4)) == std::future_status::ready;
    if (completed) stop.get();
    const auto quarantined = ClientSnapshots();
    const bool failed_safe = Restarts() != 0;
    sdk_enter.Release();
    const bool running = WaitUntil([&]() { return ClientSnapshots().front().running; });
    if (running) BeginSdkExit(client);
    const bool exited = WaitUntil([]() { return ClientSnapshots().front().exited; });
    if (exited) JoinExitedSdkForCleanup(client);
    RODAK_CHECK(entered);
    RODAK_CHECK(completed);
    RODAK_CHECK(failed_safe);
    RODAK_CHECK_EQ(quarantined.size(), 1u);
    RODAK_CHECK_FALSE(quarantined.front().running);
    RODAK_CHECK_FALSE(quarantined.front().destroyed);
    RODAK_CHECK_FALSE(quarantined.front().exited);
    RODAK_CHECK(running);
    RODAK_CHECK(exited);
    for (const auto& event : LifecycleEvents())
        RODAK_CHECK(event.action != "unsafe-destroy-attempt");
}

RODAK_TEST("credential replacement drops a dequeued old command and refuses cross-client fragment assembly") {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    PauseDequeue(true);
    f.Send(Request("old-dequeued", 9, 9));
    const bool dequeued = WaitDequeued();
    const std::string partial = Request("old-fragment", 8, 8);
    const int split = partial.size() / 2;
    Fragment(Config().mqtt_topic_shadow_desired, partial.substr(0, split), 0, partial.size());
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    PauseDequeue(false);
    CheckReplacement(f, original, "test-token-2");
    Fragment("", partial.substr(split), split, partial.size());
    f.Barrier();
    RODAK_CHECK(dequeued);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    f.Send(Request());
    auto current = Parse(f.Receipt());
    RODAK_CHECK_EQ(Get(Get(current.get(), "receipt"), "configurationRevision")->valueint, 1);
}

RODAK_TEST("credential replacement rejects a same-generation token superseded between init and attach") {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    ResetHooks reset;
    std::atomic<bool> superseded{false};
    SetSdkHook([&](SdkOperation operation, esp_mqtt_client_handle_t) {
        if (operation == SdkOperation::kInit && !superseded.exchange(true)) Rotate("test-token-3");
    });
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    CheckReplacement(f, original, "test-token-3");
    f.Barrier();
    RODAK_CHECK(superseded);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
    const auto clients = ClientSnapshots();
    RODAK_CHECK_EQ(clients.size(), 3u);
    RODAK_CHECK_EQ(clients[1].credential, "test-token-2");
    RODAK_CHECK(clients[1].destroyed);
    for (const auto& event : LifecycleEvents())
        RODAK_CHECK(!(event.client_id == clients[1].id && event.action == "start"));
}

RODAK_TEST("credential replacement accepts legacy MQTT-only snapshots when Load returns false") {
    Fixture f;
    auto config = Config();
    config.has_aiot_config = false;
    config.aiot_device_secret.clear();
    config.aiot_access_token.clear();
    config.aiot_registered = false;
    config.aiot_activated = false;
    SetConfig(config);
    rodakos::DeviceCloudConfig loaded;
    RODAK_CHECK_FALSE(f.config_service.Load(loaded));
    RODAK_CHECK(loaded.has_mqtt_config);
    f.Start();
    auto* original = CurrentClient();
    Rotate("legacy-token-2");
    Disconnect();
    RejectCredentials();
    CheckReplacement(f, original, "legacy-token-2");
    f.Barrier();
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
}

RODAK_TEST("credential replacement cancels a bound-identity snapshot whose persisted AIoT state is incomplete") {
    Fixture f;
    auto config = Config();
    config.server_requires_bound_identity = true;
    SetConfig(config);
    f.Start();
    ResetHooks reset;
    SetRefreshHook([&](unsigned call, rodakos::DeviceCloudConfig& refreshed) {
        if (call == 2) {
            // A stale HTTP result can remain complete after persisted identity became unreadable.
            auto persisted = refreshed;
            persisted.has_aiot_config = false;
            SetConfig(persisted);
        }
        return true;
    });
    Rotate("test-token-2");
    Disconnect();
    RejectCredentials();
    RODAK_CHECK(WaitUntil([]() { return ClientSnapshots().front().destroyed; }));
    RODAK_CHECK_EQ(ClientSnapshots().size(), 1u);
    RODAK_CHECK_FALSE(f.service.IsConnected());
    RODAK_CHECK(CurrentClient() == nullptr);
    RODAK_CHECK_EQ(Restarts(), 0u);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
}
