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

// Only this test TU exposes the diagnostic snapshot and state-lock seam.
#define private public
#include "phone_os/unified_mqtt_service.h"
#undef private
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
std::string ExactStop(bool display, const std::string& session, const std::string& start) {
    auto request = Request(display, "stop", session);
    request.insert(request.size() - 1, ",\"startCommandNo\":\"" + start + "\"");
    return request;
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
        RODAK_CHECK_EQ(std::string(Get(Get(Ack(number).get(), "result"), "startCommandNo")->valuestring), number);
        RODAK_CHECK(Peer(is_display).running);
    }
};

void CheckStop(const std::string& number, const std::string& session,
               const std::string& start, const char* outcome) {
    const auto ack = Ack(number);
    RODAK_CHECK_EQ(Status(ack), "ok");
    const auto* result = Get(ack.get(), "result");
    RODAK_CHECK_EQ(std::string(Get(result, "sessionId")->valuestring), session);
    RODAK_CHECK_EQ(std::string(Get(result, "startCommandNo")->valuestring), start);
    RODAK_CHECK_EQ(std::string(Get(result, "stopOutcome")->valuestring), outcome);
}

void CheckNotFound(bool display, const std::string& number) {
    const auto ack = Ack(number);
    RODAK_CHECK_EQ(Status(ack), "error");
    RODAK_CHECK_EQ(std::string(Get(ack.get(), "errorCode")->valuestring),
                   std::string(display ? "display" : "camera") + "_stream_not_found");
}
}

RODAK_TEST("MQTT exact Stop recognizes its successfully started peer after terminal cleanup") {
    for (bool display : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        streams.Start(display, "closed-start", "closed-session");
        peer.SavedCallbacks().state(ESP_PEER_STATE_DISCONNECTED);
        RODAK_CHECK(WaitUntil([&]() { return !peer.running; }));
        streams.mqtt.Barrier();
        const auto stops = peer.stop_calls.load();
        Process("late-stop", ExactStop(display, "closed-session", "closed-start"));
        CheckStop("late-stop", "closed-session", "closed-start", "already_stopped");
        Process("later-stop", ExactStop(display, "closed-session", "closed-start"));
        CheckStop("later-stop", "closed-session", "closed-start", "already_stopped");
        RODAK_CHECK_EQ(peer.stop_calls.load(), stops);
        Process("legacy-late-stop", Request(display, "stop", "closed-session"));
        CheckNotFound(display, "legacy-late-stop");
    }
}

RODAK_TEST("MQTT exact Stop freezes its original outcome and new requests observe already stopped") {
    for (bool display : {false, true}) {
        Streams streams;
        streams.Start(display, "exact-start", "exact-session");
        const auto request = ExactStop(display, "exact-session", "exact-start");
        Process("exact-stop", request);
        CheckStop("exact-stop", "exact-session", "exact-start", "stopped");
        const auto original = Wire("exact-stop").back().payload;
        const auto stops = streams.Peer(display).stop_calls.load();
        Process("exact-stop", request);
        CheckStop("exact-stop", "exact-session", "exact-start", "stopped");
        RODAK_CHECK_EQ(Wire("exact-stop").back().payload, original);
        Process("new-exact-stop", request);
        CheckStop("new-exact-stop", "exact-session", "exact-start", "already_stopped");
        RODAK_CHECK_EQ(streams.Peer(display).stop_calls.load(), stops);
    }
}

RODAK_TEST("MQTT old precise Stop cannot close a same-session replacement or replay success for it") {
    for (bool display : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        streams.Start(display, "precise-first", "reused-session");
        const auto first = ExactStop(display, "reused-session", "precise-first");
        Process("old-precise-stop", first);
        CheckStop("old-precise-stop", "reused-session", "precise-first", "stopped");
        streams.Start(display, "precise-second", "reused-session");
        const auto stops = peer.stop_calls.load();
        Process("late-old-precise-stop", first);
        CheckNotFound(display, "late-old-precise-stop");
        Process("old-precise-stop", first);
        CheckNotFound(display, "old-precise-stop");
        RODAK_CHECK_EQ(peer.stop_calls.load(), stops);
        RODAK_CHECK(peer.running);
        Process("second-precise-stop", ExactStop(display, "reused-session", "precise-second"));
        CheckStop("second-precise-stop", "reused-session", "precise-second", "stopped");
    }
}

RODAK_TEST("MQTT exact Stop rejects unknown identities and the other stream kind without native calls") {
    for (bool display : {false, true}) {
        Streams streams;
        Process("unknown-stop", ExactStop(display, "unknown", "unknown-start"));
        CheckNotFound(display, "unknown-stop");
        streams.Start(display, "known-start", "known-session");
        Process("wrong-start-stop", ExactStop(display, "known-session", "other-start"));
        CheckNotFound(display, "wrong-start-stop");
        Process("wrong-session-stop", ExactStop(display, "other-session", "known-start"));
        CheckNotFound(display, "wrong-session-stop");
        Process("wrong-kind-stop", ExactStop(!display, "known-session", "known-start"));
        CheckNotFound(!display, "wrong-kind-stop");
        RODAK_CHECK(streams.Peer(display).running);
        RODAK_CHECK_EQ(streams.Peer(display).stop_calls.load(), 0u);
        RODAK_CHECK_EQ(streams.Peer(!display).stop_calls.load(), 0u);
    }
}

RODAK_TEST("MQTT failed and synchronously terminated Start never create successful Stop proof") {
    for (bool display : {false, true}) for (bool synchronous : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        peer.start_result = synchronous;
        peer.synchronous_terminal_state = synchronous;
        Process("unaccepted-start", Request(display, "start", "unaccepted-session"));
        RODAK_CHECK_EQ(Status(Ack("unaccepted-start")), "error");
        Process("unaccepted-stop", ExactStop(display, "unaccepted-session", "unaccepted-start"));
        CheckNotFound(display, "unaccepted-stop");
    }
}

RODAK_TEST("MQTT exact Stop proof and cached success expire on connection epoch and authority reset") {
    for (bool display : {false, true}) for (bool authority : {false, true}) {
        Streams streams;
        streams.Start(display, "scope-start", "scope-session");
        const auto request = ExactStop(display, "scope-session", "scope-start");
        Process("scope-stop", request);
        CheckStop("scope-stop", "scope-session", "scope-start", "stopped");
        const auto stops = streams.Peer(display).stop_calls.load();
        if (authority) {
            std::lock_guard<std::mutex> lock(streams.mqtt.service.mqtt_mutex_);
            streams.mqtt.service.ResetEffectAuthorityLocked();
        } else {
            Disconnect();
            Connect();
        }
        Process("scope-stop", request);
        CheckNotFound(display, "scope-stop");
        Process("scope-new-stop", request);
        CheckNotFound(display, "scope-new-stop");
        RODAK_CHECK_EQ(streams.Peer(display).stop_calls.load(), stops);
    }
}

RODAK_TEST("MQTT cleanup cannot publish closed proof before native Stop returns or after epoch change") {
    for (bool display : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        streams.Start(display, "held-proof-start", "held-proof-session");
        BlockPoint stop;
        peer.before_stop_return = [&]() { stop.Enter(); };
        peer.SavedCallbacks().state(ESP_PEER_STATE_CLOSED);
        RODAK_CHECK(stop.Wait());
        {
            std::lock_guard<std::mutex> lock(streams.mqtt.service.mqtt_mutex_);
            RODAK_CHECK((display ? streams.mqtt.service.display_closed_lease_ : streams.mqtt.service.camera_closed_lease_) == nullptr);
        }
        Message(Topic("held-proof-stop"), ExactStop(display, "held-proof-session", "held-proof-start"), true);
        RODAK_CHECK(Wire("held-proof-stop").empty());
        Disconnect();
        Connect();
        stop.Release();
        RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
        peer.before_stop_return = {};
        Process("after-held-proof-stop", ExactStop(display, "held-proof-session", "held-proof-start"));
        CheckNotFound(display, "after-held-proof-stop");
        RODAK_CHECK(Wire("held-proof-stop").empty());
    }
}

RODAK_TEST("MQTT cached precise Stop remains bound after start-number eviction and instance reuse") {
    for (bool display : {false, true}) for (bool reconnect : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        streams.Start(display, "reused-start-number", "reused-session");
        for (unsigned i = 0; i < 62; ++i) {
            const auto number = "fill-" + std::to_string(i);
            Process(number, "ping");
            RODAK_CHECK_EQ(Status(Ack(number)), "ok");
        }
        const auto request = ExactStop(display, "reused-session", "reused-start-number");
        Process("retained-old-stop", request);
        CheckStop("retained-old-stop", "reused-session", "reused-start-number", "stopped");
        Process("evict-start-only", "ping");
        RODAK_CHECK_EQ(Status(Ack("evict-start-only")), "ok");
        if (reconnect) { Disconnect(); Connect(); }
        streams.Start(display, "reused-start-number", "reused-session");
        RODAK_CHECK_EQ(peer.start_calls.load(), 2u);
        Process("new-instance-stop", request);
        CheckStop("new-instance-stop", "reused-session", "reused-start-number", "stopped");
        const auto stops = peer.stop_calls.load();
        Process("retained-old-stop", request);
        CheckNotFound(display, "retained-old-stop");
        RODAK_CHECK_EQ(peer.stop_calls.load(), stops);
        Process("retained-old-stop", request + " ");
        const auto conflict = Ack("retained-old-stop");
        RODAK_CHECK_EQ(std::string(Get(conflict.get(), "errorCode")->valuestring), "command_conflict");
    }
}

RODAK_TEST("MQTT exact Stop preserves cached errors and refuses malformed instance identities") {
    for (bool display : {false, true}) {
        Streams streams;
        const auto request = ExactStop(display, "failure-session", "failure-start");
        Process("frozen-failure", request);
        CheckNotFound(display, "frozen-failure");
        streams.Start(display, "failure-start", "failure-session");
        Process("frozen-failure", request);
        CheckNotFound(display, "frozen-failure");
        unsigned malformed_index = 0;
        for (const auto& encoded : {std::string("null"), std::string("1"), std::string("\"\""),
                                    std::string("\"") + std::string(129, 'x') + "\""}) {
            auto malformed = Request(display, "stop", "failure-session");
            malformed.insert(malformed.size() - 1, ",\"startCommandNo\":" + encoded);
            const auto number = "malformed-" + std::to_string(malformed_index++);
            Process(number, malformed);
            RODAK_CHECK_EQ(Status(Ack(number)), "error");
        }
        auto nul = Request(display, "stop", "failure-session");
        nul.insert(nul.size() - 1, ",\"startCommandNo\":\"failure-start\\u0000other\"");
        Process("nul-start-identity", nul);
        RODAK_CHECK_EQ(Status(Ack("nul-start-identity")), "error");
        RODAK_CHECK(streams.Peer(display).running);
        RODAK_CHECK_EQ(streams.Peer(display).stop_calls.load(), 0u);
    }
}

RODAK_TEST("MQTT stream commands consume the full JSON input and retain bare ping compatibility") {
    for (bool display : {false, true}) {
        Streams streams;
        const auto start = Request(display, "start", "strict-session");
        Process("trailing-start", start + " trailing");
        RODAK_CHECK_EQ(Status(Ack("trailing-start")), "error");
        Process("nul-start", start + std::string("\0hidden", 7));
        RODAK_CHECK_EQ(Status(Ack("nul-start")), "error");
        RODAK_CHECK_EQ(streams.Peer(display).start_calls.load(), 0u);
        Process("strict-start", start + " \r\n\t");
        RODAK_CHECK_EQ(Status(Ack("strict-start")), "ok");
        const auto stop = ExactStop(display, "strict-session", "strict-start");
        Process("trailing-stop", stop + "{}");
        RODAK_CHECK_EQ(Status(Ack("trailing-stop")), "error");
        Process("nul-stop", stop + std::string("\0hidden", 7));
        RODAK_CHECK_EQ(Status(Ack("nul-stop")), "error");
        RODAK_CHECK(streams.Peer(display).running);
        RODAK_CHECK_EQ(streams.Peer(display).stop_calls.load(), 0u);
        Process("bare-ping", "ping");
        RODAK_CHECK_EQ(Status(Ack("bare-ping")), "ok");
        Process("strict-stop", stop + " \n");
        CheckStop("strict-stop", "strict-session", "strict-start", "stopped");
    }
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

RODAK_TEST("MQTT control gate snapshots real lock delay while preserving current and stale lease outcomes") {
    for (bool stale : {false, true}) {
        Streams streams;
        rodakos::StreamLeasePtr original;
        unsigned nonempty_calls = 0;
        streams.mqtt.service.SetWebRtcDisplayControlCallback(
            [&](const rodakos::StreamLeasePtr& lease, const std::string& payload,
                rodakos::UnifiedMqttService::DisplayControlReply reply) {
                if (!payload.empty()) { original = lease; ++nonempty_calls; }
                if (reply) reply(true, nullptr);
            });
        streams.Start(true, "timing-first", "timing-session");
        const auto control = streams.display.SavedControl();
        control("warmup", {});
        if (stale) {
            Process("timing-stop", Request(true, "stop", "timing-session"));
            streams.Start(true, "timing-new", "timing-session");
        }
        PauseDequeue(true);
        Message(Topic("timing-pause"), "ping");
        RODAK_CHECK(WaitDequeued());
        auto& service = streams.mqtt.service;
        std::unique_lock<std::mutex> state_lock(service.mqtt_mutex_);
        std::atomic<bool> entered{false};
        bool answered = false;
        bool accepted = false;
        std::thread input([&] {
            clock_read_observed = &entered;
            control("timed-control", [&](bool ok, const char*) { answered = true; accepted = ok; });
            clock_read_observed = nullptr;
        });
        const bool waiting = WaitUntil([&] { return entered.load(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(130));
        state_lock.unlock();
        input.join();
        RODAK_CHECK(waiting);
        RODAK_CHECK(answered);
        RODAK_CHECK_EQ(accepted, !stale);
        RODAK_CHECK_EQ(nonempty_calls, stale ? 1u : 2u);
        RODAK_CHECK(service.control_gate_timing_pending_);
        const auto sample = service.control_gate_timing_;
        RODAK_CHECK(sample.callback_no != 0);
        RODAK_CHECK_EQ(sample.instance_nonce, original->instance_nonce);
        RODAK_CHECK_EQ(sample.current, !stale);
        RODAK_CHECK(sample.acquired_us - sample.started_us >= 100000);
        RODAK_CHECK(sample.checked_us >= sample.acquired_us);
        RODAK_CHECK_EQ(service.control_gate_next_log_us_, sample.checked_us + 5000000);
        PauseDequeue(false);
        streams.mqtt.Barrier();
        RODAK_CHECK(WaitUntil([&] {
            std::lock_guard<std::mutex> lock(service.mqtt_mutex_);
            return !service.control_gate_timing_pending_;
        }));
        streams.mqtt.service.SetWebRtcDisplayControlCallback({});
    }
}

RODAK_TEST("MQTT display control can revoke its lease inside dispatch without holding the state lock") {
    Streams streams;
    rodakos::StreamLeasePtr dispatched;
    streams.mqtt.service.SetWebRtcDisplayControlCallback(
        [&](const rodakos::StreamLeasePtr& lease, const std::string& payload,
            rodakos::UnifiedMqttService::DisplayControlReply reply) {
            if (payload.empty()) return;
            dispatched = lease;
            Disconnect();
            if (reply) reply(lease->IsActive(), "revoked");
        });
    streams.Start(true, "reentrant-start", "reentrant-session");
    bool answered = false;
    bool accepted = true;
    streams.display.SavedControl()("disconnect-during-dispatch",
        [&](bool ok, const char*) { answered = true; accepted = ok; });
    RODAK_CHECK(answered);
    RODAK_CHECK_FALSE(accepted);
    RODAK_CHECK(dispatched != nullptr);
    RODAK_CHECK_FALSE(dispatched->IsActive());
    streams.mqtt.service.SetWebRtcDisplayControlCallback({});
}

RODAK_TEST("MQTT display Start passes the exact control lease and never reuses it for a replacement") {
    Streams streams;
    rodakos::StreamLeasePtr control_owner;
    streams.mqtt.service.SetWebRtcDisplayControlCallback(
        [&](const rodakos::StreamLeasePtr& lease, const std::string& payload,
            rodakos::UnifiedMqttService::DisplayControlReply) {
            if (!payload.empty()) control_owner = lease;
        });
    streams.Start(true, "video-lease-first", "same-video-session");
    const auto original = streams.display.SavedLease();
    streams.display.SavedControl()("same-owner", {});
    RODAK_CHECK(original != nullptr);
    RODAK_CHECK(original == control_owner);
    RODAK_CHECK(original->IsActive());
    {
        std::lock_guard<std::mutex> lock(streams.mqtt.service.mqtt_mutex_);
        RODAK_CHECK(original == streams.mqtt.service.display_lease_);
        RODAK_CHECK_EQ(original->client_generation, streams.mqtt.service.client_generation_);
        RODAK_CHECK_EQ(original->connection_epoch, streams.mqtt.service.connection_epoch_);
    }
    Process("video-lease-stop", Request(true, "stop", "same-video-session"));
    RODAK_CHECK_EQ(Status(Ack("video-lease-stop")), "ok");
    RODAK_CHECK_FALSE(original->IsActive());
    streams.Start(true, "video-lease-replacement", "same-video-session");
    const auto replacement = streams.display.SavedLease(1);
    RODAK_CHECK(replacement != nullptr);
    RODAK_CHECK(replacement != original);
    RODAK_CHECK(replacement->IsActive());
    RODAK_CHECK_EQ(replacement->session_id, original->session_id);
    RODAK_CHECK_NE(replacement->instance_nonce, original->instance_nonce);
    streams.mqtt.service.SetWebRtcDisplayControlCallback({});
}

RODAK_TEST("MQTT disconnect revokes the passed video lease before deferred cleanup can acquire its lock") {
    Streams streams;
    streams.Start(true, "video-revoke-start", "video-revoke-session");
    const auto lease = streams.display.SavedLease();
    RODAK_CHECK(lease != nullptr);
    const unsigned stops = streams.display.stop_calls.load();
    std::unique_lock<std::mutex> operation_lock(streams.mqtt.service.stream_operation_mutex_);
    Disconnect();
    const bool revoked = !lease->IsActive();
    const bool still_running = streams.display.running;
    const bool cleanup_waiting = streams.display.stop_calls == stops;
    unsigned calls = 0;
    const bool admitted = lease->TryApply([&]() { ++calls; });
    operation_lock.unlock();
    RODAK_CHECK(revoked);
    RODAK_CHECK(still_running);
    RODAK_CHECK(cleanup_waiting);
    RODAK_CHECK_FALSE(admitted);
    RODAK_CHECK_EQ(calls, 0u);
    RODAK_CHECK(WaitUntil([&]() { return !streams.display.running; }));
}

RODAK_TEST("MQTT Stop revokes the passed video lease while native Start is still in flight") {
    Streams streams;
    BlockPoint start;
    streams.display.before_start_return = [&]() { start.Enter(); };
    Message(Topic("video-start-in-flight"), Request(true, "start", "video-start-session"), true);
    const bool entered = start.Wait();
    const auto lease = entered ? streams.display.SavedLease() : nullptr;
    std::atomic<bool> stopped{false};
    std::thread stopper([&]() { streams.mqtt.service.Stop(); stopped = true; });
    const bool revoked = WaitUntil([&]() {
        return lease != nullptr ? !lease->IsActive() : !streams.mqtt.service.IsConnected();
    });
    const bool lease_inactive = lease != nullptr && !lease->IsActive();
    const bool waited_for_start = !stopped.load();
    start.Release();
    stopper.join();
    streams.display.before_start_return = {};
    RODAK_CHECK(entered);
    RODAK_CHECK(lease != nullptr);
    RODAK_CHECK(revoked);
    RODAK_CHECK(lease_inactive);
    RODAK_CHECK(waited_for_start);
    RODAK_CHECK_FALSE(streams.display.running);
    RODAK_CHECK_EQ(streams.display.maximum_overlapping_operations.load(), 1u);
    RODAK_CHECK(Wire("video-start-in-flight").empty());
}
