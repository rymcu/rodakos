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
// Observe completion only; network transitions enter the registered event handler.
#define private public
#include "phone_os/unified_mqtt_service.h"
#undef private
#include "service_fixture.h"
#include "phone_os/voice_wake_service.h"

#include <arpa/inet.h>
#include <condition_variable>
#include <future>

using namespace mqtt_host;

namespace {
const uint32_t kIpA = inet_addr("192.168.137.20");
const uint32_t kIpB = inet_addr("192.168.88.20");
const uint32_t kIpC = inet_addr("192.168.99.20");
const uint32_t kMask = inet_addr("255.255.255.0");
const uint32_t kGatewayA = inet_addr("192.168.137.1");
const uint32_t kGatewayB = inet_addr("192.168.88.1");
const uint32_t kGatewayC = inet_addr("192.168.99.1");

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

void Rotate(const char* credential) {
    auto config = Config();
    config.mqtt_password = credential;
    SetConfig(config);
}

void StartOnA(Fixture& fixture) {
    SetStationRoute(kIpA, kMask, kGatewayA);
    fixture.Start();
}

void WaitSettled(Fixture& fixture) {
    RODAK_CHECK(WaitUntil([&]() {
        std::lock_guard<std::mutex> lock(fixture.service.mqtt_mutex_);
        return fixture.service.connected_.load() && !fixture.service.reset_scheduled_.load() &&
            !fixture.service.network_route_refresh_scheduled_;
    }));
    fixture.Barrier();
}

void ExpectNotStarted(const ClientSnapshot& candidate) {
    RODAK_CHECK(candidate.destroyed);
    for (const auto& event : LifecycleEvents())
        RODAK_CHECK(!(event.client_id == candidate.id && event.action == "start"));
}

rodakos::DeviceCloudConfig PinConfig() {
    auto config = Config();
    config.server_trust.server_id = std::string(64, 'a');
    config.server_trust.tls_name = "rodak-aaaaaaaaaaaaaaaa.local";
    config.server_trust.ca_pem = std::string(800, 'C');
    config.server_connect_address = "192.168.137.9";
    config.mqtt_broker_address = config.server_trust.tls_name;
    config.mqtt_broker_port = 8883;
    SetConfig(config);
    return config;
}

void ExpectRouteRestart(const std::function<void(rodakos::DeviceCloudConfig&)>& change,
                        bool pinned = true) {
    Fixture f;
    auto config = pinned ? PinConfig() : Config();
    StartOnA(f);
    auto* original = CurrentClient();
    if (pinned) config.server_connect_address = "192.168.88.9";
    change(config);
    SetConfig(config);
    GotIp(kIpB, kMask, kGatewayB);
    RODAK_CHECK(WaitUntil([]() { return Restarts() != 0; }));
    RODAK_CHECK(CurrentClient() == original);
    RODAK_CHECK_EQ(ClientSnapshots().size(), 1u);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
}

std::string ExecuteCommand(Fixture& fixture, const std::string& number,
                            const std::string& request) {
    const auto topic = "devices/test-device/commands/" + number;
    const size_t previous = WirePublications().size();
    Message(topic, request);
    RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
    fixture.Barrier();
    esp_mqtt_event_t flush;
    flush.event_id = MQTT_USER_EVENT;
    Deliver(flush);
    const auto sent = WirePublications();
    for (size_t index = previous; index < sent.size(); ++index) {
        if (sent[index].topic != topic + "/ack") continue;
        const auto body = Parse(sent[index].payload);
        const auto* result = Get(body.get(), "result");
        if (Get(result, "cameraStream") == nullptr) return sent[index].payload;
    }
    RODAK_CHECK(false);
    return {};
}
}

RODAK_TEST("GOT_IP first address starts one client and duplicate route does not refresh") {
    Fixture f;
    SetWifiConnected(false);
    RODAK_CHECK(f.service.Start());
    RODAK_CHECK(CurrentClient() == nullptr);
    RODAK_CHECK_EQ(RefreshCalls(), 0u);
    GotIp(kIpA, kMask, kGatewayA);
    WaitSettled(f);
    auto* original = CurrentClient();
    GotIp(kIpA, kMask, kGatewayA);
    f.Barrier();
    RODAK_CHECK(CurrentClient() == original);
    RODAK_CHECK_EQ(RefreshCalls(), 1u);
    RODAK_CHECK_EQ(ClientSnapshots().size(), 1u);
}

RODAK_TEST("GOT_IP matches the already connected station baseline without rotating credentials") {
    Fixture f;
    StartOnA(f);
    auto* original = CurrentClient();
    GotIp(kIpA, kMask, kGatewayA);
    f.Barrier();
    RODAK_CHECK(CurrentClient() == original);
    RODAK_CHECK_EQ(RefreshCalls(), 1u);
}

RODAK_TEST("GOT_IP changed address replaces a still connected client and coalesces duplicate events") {
    Fixture f;
    StartOnA(f);
    auto* original = CurrentClient();
    const auto http = std::make_shared<Gate>();
    ResetHooks reset;
    SetRefreshHook([http](unsigned call, rodakos::DeviceCloudConfig&) {
        return call != 2 || http->Enter();
    });
    Rotate("route-token-2");
    GotIp(kIpB, kMask, kGatewayB);
    const bool entered = http->Wait();
    GotIp(kIpB, kMask, kGatewayB);
    Connect();
    http->Release();
    RODAK_CHECK(entered);
    f.CheckRefreshedClient(original, "route-token-2");
    WaitSettled(f);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
    RODAK_CHECK_EQ(ClientSnapshots().size(), 2u);
}

RODAK_TEST("GOT_IP detects gateway and netmask changes even when the station address is unchanged") {
    Fixture f;
    StartOnA(f);
    auto* original = CurrentClient();
    GotIp(kIpA, kMask, inet_addr("192.168.137.2"));
    f.CheckRefreshedClient(original, "test-token-1");
    WaitSettled(f);
    auto* second = CurrentClient();
    GotIp(kIpA, inet_addr("255.255.0.0"), inet_addr("192.168.137.2"));
    f.CheckRefreshedClient(second, "test-token-1");
    WaitSettled(f);
    RODAK_CHECK_EQ(RefreshCalls(), 3u);
}

RODAK_TEST("GOT_IP during bootstrap rejects the old route and refreshes latest before attachment") {
    Fixture f;
    SetStationRoute(kIpA, kMask, kGatewayA);
    const auto http = std::make_shared<Gate>();
    ResetHooks reset;
    SetRefreshHook([http](unsigned call, rodakos::DeviceCloudConfig&) {
        return call != 1 || http->Enter();
    });
    RODAK_CHECK(f.service.Start());
    const bool entered = http->Wait();
    GotIp(kIpB, kMask, kGatewayB);
    http->Release();
    RODAK_CHECK(entered);
    WaitSettled(f);
    const auto clients = ClientSnapshots();
    RODAK_CHECK(!clients.empty());
    for (size_t index = 0; index + 1 < clients.size(); ++index) ExpectNotStarted(clients[index]);
    RODAK_CHECK_EQ(clients.back().credential, "test-token-1");
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
}

RODAK_TEST("GOT_IP during refresh retains the newer route request and drops intermediate credentials") {
    Fixture f;
    StartOnA(f);
    auto* original = CurrentClient();
    const auto http = std::make_shared<Gate>();
    ResetHooks reset;
    SetRefreshHook([http](unsigned call, rodakos::DeviceCloudConfig&) {
        return call != 2 || http->Enter();
    });
    Rotate("route-token-2");
    GotIp(kIpB, kMask, kGatewayB);
    const bool entered = http->Wait();
    Rotate("route-token-3");
    GotIp(kIpC, kMask, kGatewayC);
    http->Release();
    RODAK_CHECK(entered);
    f.CheckRefreshedClient(original, "route-token-3");
    WaitSettled(f);
    RODAK_CHECK_EQ(RefreshCalls(), 3u);
    RODAK_CHECK_EQ(ClientSnapshots().size(), 2u);
}

RODAK_TEST("GOT_IP during SDK init destroys the stale candidate before any start") {
    Fixture f;
    StartOnA(f);
    const auto init = std::make_shared<Gate>();
    ResetHooks reset;
    const auto first_init = std::make_shared<std::atomic<bool>>(true);
    SetSdkHook([first_init, init](SdkOperation operation, esp_mqtt_client_handle_t) {
        if (operation == SdkOperation::kInit && first_init->exchange(false)) (void)init->Enter();
    });
    GotIp(kIpB, kMask, kGatewayB);
    const bool entered = init->Wait();
    GotIp(kIpC, kMask, kGatewayC);
    init->Release();
    RODAK_CHECK(entered);
    WaitSettled(f);
    const auto clients = ClientSnapshots();
    RODAK_CHECK_EQ(clients.size(), 3u);
    RODAK_CHECK_EQ(clients[1].credential, "test-token-1");
    ExpectNotStarted(clients[1]);
    RODAK_CHECK_EQ(clients.back().credential, "test-token-1");
    RODAK_CHECK_EQ(RefreshCalls(), 3u);
    RODAK_CHECK_EQ(Restarts(), 0u);
}

RODAK_TEST("GOT_IP defers network refresh while voice is active and retains it after duplicate route") {
    rodakos::VoiceWakeService voice;
    Fixture f;
    f.service.SetVoiceWakeService(&voice);
    StartOnA(f);
    auto* original = CurrentClient();
    auto state = voice.GetState();
    state.status = rodakos::VoiceWakeStatus::kAssistantActive;
    voice.SetState(state);
    Rotate("route-token-2");
    GotIp(kIpB, kMask, kGatewayB);
    f.Barrier();
    GotIp(kIpB, kMask, kGatewayB);
    f.Barrier();
    RODAK_CHECK(CurrentClient() == original);
    RODAK_CHECK_EQ(RefreshCalls(), 1u);
    state.status = rodakos::VoiceWakeStatus::kDisabled;
    voice.SetState(state);
    f.CheckRefreshedClient(original, "route-token-2");
    WaitSettled(f);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
}

RODAK_TEST("GOT_IP does not erase an auth rejection even if the old transport reconnects") {
    rodakos::VoiceWakeService voice;
    Fixture f;
    f.service.SetVoiceWakeService(&voice);
    StartOnA(f);
    auto* original = CurrentClient();
    auto state = voice.GetState();
    state.status = rodakos::VoiceWakeStatus::kAssistantActive;
    voice.SetState(state);
    Rotate("route-token-2");
    GotIp(kIpB, kMask, kGatewayB);
    RejectCredentials();
    Connect();
    GotIp(kIpB, kMask, kGatewayB);
    f.Barrier();
    RODAK_CHECK_EQ(RefreshCalls(), 1u);
    state.status = rodakos::VoiceWakeStatus::kDisabled;
    voice.SetState(state);
    f.CheckRefreshedClient(original, "route-token-2");
    WaitSettled(f);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
}

RODAK_TEST("GOT_IP in-flight refresh is cancelled by Stop and later events have no registered consumer") {
    Fixture f;
    StartOnA(f);
    const auto http = std::make_shared<Gate>();
    ResetHooks reset;
    SetRefreshHook([http](unsigned call, rodakos::DeviceCloudConfig&) {
        return call != 2 || http->Enter();
    });
    GotIp(kIpB, kMask, kGatewayB);
    const bool entered = http->Wait();
    auto stop = std::async(std::launch::async, [&]() { f.service.Stop(); });
    const bool admission_closed = WaitUntil([&]() { return !f.service.started_.load(); });
    GotIp(kIpC, kMask, kGatewayC);
    http->Release();
    const bool finished = stop.wait_for(std::chrono::seconds(4)) == std::future_status::ready;
    if (finished) stop.get();
    RODAK_CHECK(entered);
    RODAK_CHECK(admission_closed);
    RODAK_CHECK(finished);
    GotIp(kIpA, kMask, kGatewayA);
    RODAK_CHECK_EQ(ClientSnapshots().size(), 1u);
    RODAK_CHECK(ClientSnapshots().front().destroyed);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
    RODAK_CHECK_FALSE(f.service.reset_scheduled_.load());
    RODAK_CHECK_FALSE(f.service.connecting_.load());
    RODAK_CHECK_EQ(Restarts(), 0u);
}

RODAK_TEST("GOT_IP first known route fences an initial HTTP request with no saved route baseline") {
    Fixture f;
    SetStationRoute(0, 0, 0);
    const auto http = std::make_shared<Gate>();
    ResetHooks reset;
    SetRefreshHook([http](unsigned call, rodakos::DeviceCloudConfig&) {
        return call != 1 || http->Enter();
    });
    RODAK_CHECK(f.service.Start());
    const bool entered = http->Wait();
    GotIp(kIpA, kMask, kGatewayA);
    http->Release();
    RODAK_CHECK(entered);
    WaitSettled(f);
    const auto clients = ClientSnapshots();
    RODAK_CHECK(!clients.empty());
    for (size_t index = 0; index + 1 < clients.size(); ++index) ExpectNotStarted(clients[index]);
    RODAK_CHECK(clients.back().connected);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
}

RODAK_TEST("GOT_IP zero address and null payload do not replace or erase the known station route") {
    Fixture f;
    StartOnA(f);
    auto* original = CurrentClient();
    GotIp(0, kMask, kGatewayA);
    rodakos::UnifiedMqttService::NetworkEventHandler(&f.service, IP_EVENT,
                                                    IP_EVENT_STA_GOT_IP, nullptr);
    GotIp(kIpA, kMask, kGatewayA);
    f.Barrier();
    RODAK_CHECK(CurrentClient() == original);
    RODAK_CHECK_EQ(RefreshCalls(), 1u);
    RODAK_CHECK_EQ(ClientSnapshots().size(), 1u);
}

RODAK_TEST("GOT_IP changed in SDK registration fences a candidate even with identical credentials") {
    Fixture f;
    StartOnA(f);
    ResetHooks reset;
    const auto moved = std::make_shared<std::atomic<bool>>(false);
    SetSdkHook([moved](SdkOperation operation, esp_mqtt_client_handle_t) {
        if (operation == SdkOperation::kRegister && !moved->exchange(true))
            GotIp(kIpC, kMask, kGatewayC);
    });
    GotIp(kIpB, kMask, kGatewayB);
    WaitSettled(f);
    const auto clients = ClientSnapshots();
    RODAK_CHECK(moved->load());
    RODAK_CHECK_EQ(clients.size(), 3u);
    ExpectNotStarted(clients[1]);
    RODAK_CHECK_EQ(clients.back().credential, "test-token-1");
    RODAK_CHECK_EQ(RefreshCalls(), 3u);
    RODAK_CHECK_EQ(Restarts(), 0u);
}

RODAK_TEST("GOT_IP pinned numeric route preserves historical effects without reapplying an old volume") {
    Fixture f;
    auto config = PinConfig();
    StartOnA(f);
    auto* original = CurrentClient();
    f.Send(Request("before-route", 30, 1));
    const auto first = f.Receipt();
    f.Send(Request("newer-volume", 55, 2));
    (void)f.Receipt(1);
    RODAK_CHECK_EQ(f.output.volume(), 55);
    config.server_connect_address = "192.168.88.9";
    SetConfig(config);
    GotIp(kIpB, kMask, kGatewayB);
    f.CheckRefreshedClient(original, "test-token-1");
    WaitSettled(f);
    RODAK_CHECK_EQ(BrokerTls().uri, "mqtts://192.168.88.9:8883");
    RODAK_CHECK_EQ(BrokerTls().common_name, config.server_trust.tls_name);
    f.Send(Request("before-route", 30, 1));
    RODAK_CHECK_EQ(f.Receipt(2), first);
    RODAK_CHECK_EQ(f.output.volume(), 55);
    RODAK_CHECK_EQ(RefreshCalls(), 2u);
}

RODAK_TEST("GOT_IP pinned numeric route preserves command ACK without restarting a retired stream") {
    rodakos::WebRtcCameraService camera;
    Fixture f;
    camera.start_result = true;
    f.service.SetWebRtcCameraService(&camera);
    auto config = PinConfig();
    StartOnA(f);
    auto* original = CurrentClient();
    const std::string request = "{\"command\":\"camera.stream.start\",\"sessionId\":\"route-stream\"}";
    const auto first = ExecuteCommand(f, "route-command", request);
    RODAK_CHECK_EQ(std::string(Get(Parse(first).get(), "status")->valuestring), "ok");
    RODAK_CHECK_EQ(camera.start_calls.load(), 1u);
    config.server_connect_address = "192.168.88.9";
    SetConfig(config);
    GotIp(kIpB, kMask, kGatewayB);
    f.CheckRefreshedClient(original, "test-token-1");
    WaitSettled(f);
    RODAK_CHECK_FALSE(camera.running);
    RODAK_CHECK_EQ(ExecuteCommand(f, "route-command", request), first);
    RODAK_CHECK_EQ(camera.start_calls.load(), 1u);
    RODAK_CHECK_FALSE(camera.running);
}

RODAK_TEST("GOT_IP does not permit unpinned legacy broker migration without restart") {
    ExpectRouteRestart([](rodakos::DeviceCloudConfig& config) {
        config.mqtt_broker_address = "other-host-broker";
    }, false);
}

RODAK_TEST("GOT_IP does not treat a changed certificate as the original pinned authority") {
    ExpectRouteRestart([](rodakos::DeviceCloudConfig& config) {
        config.server_trust.ca_pem = std::string(800, 'D');
    });
}

RODAK_TEST("GOT_IP same pin does not grant a changed broker port without restart") {
    ExpectRouteRestart([](rodakos::DeviceCloudConfig& config) {
        config.mqtt_broker_port = 8884;
    });
}

RODAK_TEST("GOT_IP same pin does not grant changed command topics without restart") {
    ExpectRouteRestart([](rodakos::DeviceCloudConfig& config) {
        config.mqtt_topic_commands = "devices/other-device/commands/+";
    });
}
