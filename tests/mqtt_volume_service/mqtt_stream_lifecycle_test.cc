#include "service_fixture.h"
#include "phone_os/webrtc_display_service.h"

using namespace mqtt_host;

namespace {
struct BlockPoint {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    ~BlockPoint() { Release(); }
    void Enter() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&]() { return released; });
    }
    bool Wait() {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(3), [&]() { return entered; });
    }
    void Release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        changed.notify_all();
    }
};
std::string Topic(const std::string& number) { return "devices/test-device/commands/" + number; }
std::string Request(bool display, const std::string& action, const std::string& session) {
    return "{\"command\":\"" + std::string(display ? "display" : "camera") + ".stream." +
        action + "\",\"sessionId\":\"" + session + "\",\"type\":\"candidate\",\"data\":\"aWNl\"}";
}
void Process(const std::string& number, const std::string& payload) {
    Message(Topic(number), payload, true);
    RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
}
void Drain() { esp_mqtt_event_t event; event.event_id = MQTT_USER_EVENT; Deliver(event); }
std::vector<Publication> Wire(const std::string& number) {
    std::vector<Publication> result;
    for (const auto& item : WirePublications())
        if (item.topic == Topic(number) + "/ack") result.push_back(item);
    return result;
}
Json Ack(const std::string& number) {
    Drain();
    const auto items = Wire(number);
    for (auto item = items.rbegin(); item != items.rend(); ++item) {
        auto json = Parse(item->payload);
        const auto* result = Get(json.get(), "result");
        if (Get(result, "cameraStream") == nullptr && Get(result, "displayStream") == nullptr)
            return json;
    }
    return Json(nullptr, cJSON_Delete);
}
std::string Status(const Json& value) {
    const char* status = cJSON_GetStringValue(Get(value.get(), "status"));
    return status == nullptr ? std::string() : status;
}
struct Streams {
    rodakos::WebRtcCameraService camera;
    rodakos::WebRtcDisplayService display;
    Fixture mqtt;
    Streams() {
        camera.start_result = display.start_result = true;
        camera.remote_result = display.remote_result = true;
        mqtt.service.SetWebRtcCameraService(&camera);
        mqtt.service.SetWebRtcDisplayService(&display);
        mqtt.Start();
        HoldUserEvents(true);
    }
    rodakos::WebRtcCameraService& Peer(bool is_display) {
        return is_display ? static_cast<rodakos::WebRtcCameraService&>(display) : camera;
    }
    void Start(bool is_display, const std::string& number, const std::string& session) {
        Process(number, Request(is_display, "start", session));
        RODAK_CHECK_EQ(Status(Ack(number)), "ok");
        RODAK_CHECK(Peer(is_display).running);
    }
};
}

RODAK_TEST("MQTT revoked queued camera and display starts never enter native peers") {
    for (bool display : {false, true}) {
        Streams streams;
        PauseDequeue(true);
        Message(Topic("queued-start"), Request(display, "start", "queued-session"), true);
        const bool dequeued = WaitDequeued();
        Disconnect();
        Connect();
        PauseDequeue(false);
        streams.mqtt.Barrier();
        Drain();
        RODAK_CHECK(dequeued);
        RODAK_CHECK_EQ(streams.Peer(display).start_calls.load(), 0u);
        RODAK_CHECK(Wire("queued-start").empty());
        streams.Start(display, "fresh-start", "fresh-session");
    }
}

RODAK_TEST("MQTT Stop cancels a dequeued stream Start before native peer admission") {
    for (bool display : {false, true}) {
        Streams streams;
        PauseDequeue(true);
        Message(Topic("stop-queued"), Request(display, "start", "queued-session"), true);
        const bool dequeued = WaitDequeued();
        std::thread stopper([&]() { streams.mqtt.service.Stop(); });
        const bool revoked = WaitUntil([&]() { return !streams.mqtt.service.IsConnected(); });
        PauseDequeue(false);
        stopper.join();
        RODAK_CHECK(dequeued);
        RODAK_CHECK(revoked);
        RODAK_CHECK_EQ(streams.Peer(display).start_calls.load(), 0u);
        RODAK_CHECK_FALSE(streams.Peer(display).running);
        RODAK_CHECK(Wire("stop-queued").empty());
    }
}

RODAK_TEST("MQTT disconnect during native Start revokes promptly and stops the completed stale peer") {
    for (bool display : {false, true}) {
        Streams streams;
        BlockPoint start;
        auto& peer = streams.Peer(display);
        peer.before_start_return = [&]() { start.Enter(); };
        Message(Topic("disconnect-start"), Request(display, "start", "held-session"), true);
        const bool entered = start.Wait();
        std::atomic<bool> disconnected{false};
        std::thread sdk([&]() { Disconnect(); disconnected = true; });
        const bool callback_did_not_wait = WaitUntil([&]() { return disconnected.load(); });
        start.Release();
        sdk.join();
        RODAK_CHECK(entered);
        RODAK_CHECK(callback_did_not_wait);
        RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
        RODAK_CHECK(WaitUntil([&]() { return !peer.running; }));
        RODAK_CHECK(peer.stop_calls > 0);
        RODAK_CHECK_EQ(peer.maximum_overlapping_operations.load(), 1u);
        RODAK_CHECK(Wire("disconnect-start").empty());
        peer.before_start_return = {};
        Connect();
        streams.Start(display, "reconnected-start", "held-session");
    }
}

RODAK_TEST("MQTT Stop during native Start waits for it then stops its own instance without overlap") {
    for (bool display : {false, true}) {
        Streams streams;
        BlockPoint start;
        auto& peer = streams.Peer(display);
        peer.before_start_return = [&]() { start.Enter(); };
        Message(Topic("stop-during-start"), Request(display, "start", "held-session"), true);
        const bool entered = start.Wait();
        std::atomic<bool> stopped{false};
        std::thread stopper([&]() { streams.mqtt.service.Stop(); stopped = true; });
        const bool revoked = WaitUntil([&]() { return !streams.mqtt.service.IsConnected(); });
        const bool waited = !stopped;
        start.Release();
        stopper.join();
        RODAK_CHECK(entered);
        RODAK_CHECK(revoked);
        RODAK_CHECK(waited);
        RODAK_CHECK_FALSE(peer.running);
        RODAK_CHECK_EQ(peer.maximum_overlapping_operations.load(), 1u);
        RODAK_CHECK_EQ(fake_stream::live_peers.load(), 0u);
        RODAK_CHECK(Wire("stop-during-start").empty());
    }
}

RODAK_TEST("MQTT disconnect after Start makes the worker stop the old peer before new epoch reentry") {
    for (bool display : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        streams.Start(display, "running-old", "same-session");
        const auto old_callbacks = peer.SavedCallbacks();
        const auto old_count = Wire("running-old").size();
        Disconnect();
        RODAK_CHECK(WaitUntil([&]() { return !peer.running; }));
        RODAK_CHECK(peer.stop_calls > 0);
        Connect();
        streams.Start(display, "running-new", "same-session");
        old_callbacks.signal(ESP_PEER_MSG_TYPE_SDP, {'o', 'l', 'd'});
        old_callbacks.state(ESP_PEER_STATE_CLOSED);
        Process("signal-new", Request(display, "signal", "same-session"));
        RODAK_CHECK_EQ(Status(Ack("signal-new")), "ok");
        RODAK_CHECK_EQ(peer.last_remote_instance.load(), 2u);
        RODAK_CHECK(peer.running);
        RODAK_CHECK_EQ(Wire("running-old").size(), old_count);
        RODAK_CHECK_EQ(fake_stream::maximum_live_peers.load(), 1u);
    }
}

RODAK_TEST("MQTT stream Stop can join a terminal callback on another thread without holding callback locks") {
    for (bool display : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        peer.terminal_on_stop_thread = true;
        streams.Start(display, "join-start", "join-session");
        Process("join-stop", Request(display, "stop", "join-session"));
        RODAK_CHECK_EQ(Status(Ack("join-stop")), "ok");
        RODAK_CHECK_FALSE(peer.running);
        RODAK_CHECK_EQ(peer.terminal_callback_returns.load(), 1u);
        RODAK_CHECK_EQ(peer.maximum_overlapping_operations.load(), 1u);
        bool terminal = false;
        for (const auto& publication : Wire("join-start")) {
            const auto body = Parse(publication.payload);
            const auto* stream = Get(Get(body.get(), "result"), display ? "displayStream" : "cameraStream");
            if (stream != nullptr && std::string(Get(stream, "event")->valuestring) == "state") terminal = true;
        }
        RODAK_CHECK(terminal);
    }
}

RODAK_TEST("MQTT stale terminal callback with the same session string cannot revoke a newer instance") {
    for (bool display : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        streams.Start(display, "same-id-first", "shared-id");
        const auto old_callbacks = peer.SavedCallbacks();
        Process("same-id-stop", Request(display, "stop", "shared-id"));
        RODAK_CHECK_EQ(Status(Ack("same-id-stop")), "ok");
        streams.Start(display, "same-id-second", "shared-id");
        const auto old_count = Wire("same-id-first").size();
        old_callbacks.state(ESP_PEER_STATE_CLOSED);
        old_callbacks.signal(ESP_PEER_MSG_TYPE_CANDIDATE, {'s', 't', 'a', 'l', 'e'});
        Process("same-id-current-signal", Request(display, "signal", "shared-id"));
        RODAK_CHECK_EQ(Status(Ack("same-id-current-signal")), "ok");
        RODAK_CHECK_EQ(peer.last_remote_instance.load(), 2u);
        RODAK_CHECK_EQ(Wire("same-id-first").size(), old_count);
        RODAK_CHECK(peer.running);
        Process("same-id-final-stop", Request(display, "stop", "shared-id"));
        RODAK_CHECK_EQ(Status(Ack("same-id-final-stop")), "ok");
        RODAK_CHECK_FALSE(peer.running);
    }
}

RODAK_TEST("MQTT synchronous terminal during Start stops the instance and never acknowledges successful start") {
    for (bool display : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        peer.synchronous_signal = true;
        peer.synchronous_terminal_state = true;
        Process("sync-terminal", Request(display, "start", "terminal-session"));
        RODAK_CHECK_EQ(Status(Ack("sync-terminal")), "error");
        RODAK_CHECK_FALSE(peer.running);
        RODAK_CHECK(peer.stop_calls > 0);
        RODAK_CHECK_EQ(peer.maximum_overlapping_operations.load(), 1u);
        peer.synchronous_terminal_state = false;
        streams.Start(display, "after-sync-terminal", "terminal-session");
    }
}

RODAK_TEST("MQTT failed Start suppresses queued nonterminal signals from the rejected lease") {
    for (bool display : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        peer.start_result = false;
        peer.synchronous_signal = true;
        Process("failed-native-start", Request(display, "start", "failed-session"));
        RODAK_CHECK_EQ(Status(Ack("failed-native-start")), "error");
        RODAK_CHECK_FALSE(peer.running);
        RODAK_CHECK_EQ(Wire("failed-native-start").size(), size_t{1});
        peer.start_result = true;
        streams.Start(display, "after-failed-start", "failed-session");
    }
}

RODAK_TEST("MQTT camera and display cannot overlap and cleanup finishes before admitting the other peer") {
    for (bool first_display : {false, true}) {
        Streams streams;
        streams.Start(first_display, "exclusive-first", "exclusive-one");
        Process("exclusive-busy", Request(!first_display, "start", "exclusive-two"));
        RODAK_CHECK_EQ(Status(Ack("exclusive-busy")), "error");
        RODAK_CHECK_EQ(streams.Peer(!first_display).start_calls.load(), 0u);
        BlockPoint stop;
        streams.Peer(first_display).before_stop_return = [&]() { stop.Enter(); };
        streams.Peer(first_display).SavedCallbacks().state(ESP_PEER_STATE_CLOSED);
        const bool stopping = stop.Wait();
        Message(Topic("exclusive-next"), Request(!first_display, "start", "exclusive-two"), true);
        const bool not_started_early = streams.Peer(!first_display).start_calls == 0;
        stop.Release();
        RODAK_CHECK(stopping);
        RODAK_CHECK(not_started_early);
        RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
        RODAK_CHECK_EQ(Status(Ack("exclusive-next")), "ok");
        RODAK_CHECK_FALSE(streams.Peer(first_display).running);
        RODAK_CHECK(streams.Peer(!first_display).running);
        RODAK_CHECK_EQ(fake_stream::maximum_live_peers.load(), 1u);
        streams.Peer(first_display).before_stop_return = {};
    }
}

RODAK_TEST("MQTT native disconnect callback never waits for a blocked stream operation") {
    Streams streams;
    streams.Start(false, "blocking-stop-start", "blocking-stop-session");
    BlockPoint stop;
    streams.camera.before_stop_return = [&]() { stop.Enter(); };
    std::thread stopper([&]() { streams.mqtt.service.StopWebRtcCameraStream(); });
    const bool entered = stop.Wait();
    std::atomic<bool> disconnected{false};
    std::thread sdk([&]() { Disconnect(); disconnected = true; });
    const bool returned_without_operation_lock = WaitUntil([&]() { return disconnected.load(); });
    stop.Release();
    sdk.join();
    stopper.join();
    RODAK_CHECK(entered);
    RODAK_CHECK(returned_without_operation_lock);
    RODAK_CHECK_FALSE(streams.camera.running);
    RODAK_CHECK_EQ(streams.camera.maximum_overlapping_operations.load(), 1u);
    streams.camera.before_stop_return = {};
    Connect();
    streams.Start(false, "after-blocking-stop", "blocking-stop-session");
}

RODAK_TEST("MQTT disconnect during a native signal waits for worker cleanup without overlapping Stop") {
    for (bool display : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        streams.Start(display, "signal-start", "signal-session");
        BlockPoint remote;
        peer.before_remote_return = [&]() { remote.Enter(); };
        Message(Topic("signal-in-flight"), Request(display, "signal", "signal-session"), true);
        const bool entered = remote.Wait();
        std::atomic<bool> disconnected{false};
        std::thread sdk([&]() { Disconnect(); disconnected = true; });
        const bool revoked_without_waiting = WaitUntil([&]() { return disconnected.load(); });
        remote.Release();
        sdk.join();
        RODAK_CHECK(entered);
        RODAK_CHECK(revoked_without_waiting);
        RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
        RODAK_CHECK(WaitUntil([&]() { return !peer.running; }));
        RODAK_CHECK_EQ(peer.remote_calls.load(), 1u);
        RODAK_CHECK_EQ(peer.maximum_overlapping_operations.load(), 1u);
        RODAK_CHECK(Wire("signal-in-flight").empty());
        peer.before_remote_return = {};
        Connect();
        streams.Start(display, "after-signal-reconnect", "signal-session");
    }
}

RODAK_TEST("MQTT data-channel terminal states retain normalized feedback but old nonce cannot reach a replacement") {
    for (bool display : {false, true}) {
        for (const auto terminal : {ESP_PEER_STATE_DATA_CHANNEL_CLOSED, ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED}) {
            Streams streams;
            auto& peer = streams.Peer(display);
            streams.Start(display, "channel-old", "channel-session");
            const auto old = peer.SavedCallbacks();
            old.state(terminal);
            RODAK_CHECK(WaitUntil([&]() { return !peer.running; }));
            Drain();
            const auto sent = Wire("channel-old");
            RODAK_CHECK_EQ(sent.size(), size_t{2});
            const auto body = Parse(sent.back().payload);
            const auto* state = Get(Get(body.get(), "result"), display ? "displayStream" : "cameraStream");
            RODAK_CHECK(state != nullptr);
            RODAK_CHECK_EQ(std::string(Get(state, "type")->valuestring),
                terminal == ESP_PEER_STATE_DATA_CHANNEL_CLOSED ? "closed" : "disconnected");
            streams.Start(display, "channel-new", "channel-session");
            old.state(terminal);
            Process("channel-new-signal", Request(display, "signal", "channel-session"));
            RODAK_CHECK_EQ(Status(Ack("channel-new-signal")), "ok");
            RODAK_CHECK(peer.running);
            RODAK_CHECK_EQ(Wire("channel-old").size(), size_t{2});
        }
    }
}

RODAK_TEST("MQTT display control captures exact lease and rejects stale data without retargeting teardown") {
    Streams streams;
    std::vector<rodakos::StreamLeasePtr> owners;
    std::vector<std::string> frames;
    streams.mqtt.service.SetWebRtcDisplayControlCallback(
        [&](const rodakos::StreamLeasePtr& lease, const std::string& payload,
            rodakos::UnifiedMqttService::DisplayControlReply reply) {
            owners.push_back(lease);
            frames.push_back(payload);
            if (reply) reply(true, "queued");
        });
    streams.Start(true, "control-first", "same-control-id");
    const auto old_control = streams.display.SavedControl();
    old_control("delayed-old-frame", {});
    RODAK_CHECK_EQ(owners.size(), size_t{1});
    const auto old_lease = owners.front();
    RODAK_CHECK(old_lease->IsActive());
    Process("control-stop", Request(true, "stop", "same-control-id"));
    RODAK_CHECK_EQ(Status(Ack("control-stop")), "ok");
    streams.Start(true, "control-second", "same-control-id");
    streams.display.SavedControl(1)("new-frame", {});
    RODAK_CHECK_EQ(owners.size(), size_t{2});
    const auto new_lease = owners.back();
    RODAK_CHECK_NE(old_lease->instance_nonce, new_lease->instance_nonce);
    RODAK_CHECK_EQ(old_lease->session_id, new_lease->session_id);
    unsigned applied = 0;
    RODAK_CHECK_FALSE(old_lease->TryApply([&]() { ++applied; }));
    RODAK_CHECK(new_lease->TryApply([&]() { ++applied; }));
    RODAK_CHECK_EQ(applied, 1u);
    bool answered = false;
    bool accepted = true;
    old_control("late-old-frame", [&](bool ok, const char*) { answered = true; accepted = ok; });
    RODAK_CHECK(answered);
    RODAK_CHECK_FALSE(accepted);
    RODAK_CHECK_EQ(owners.size(), size_t{2});
    old_control("", {});
    RODAK_CHECK_EQ(owners.size(), size_t{3});
    RODAK_CHECK(owners.back() == old_lease);
    RODAK_CHECK(frames.back().empty());
    RODAK_CHECK_FALSE(old_lease->IsActive());
    RODAK_CHECK(new_lease->IsActive());
    streams.mqtt.service.SetWebRtcDisplayControlCallback({});
}
