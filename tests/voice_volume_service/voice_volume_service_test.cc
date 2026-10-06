#include "host_runtime.h"
#include "test_framework.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/realtime_voice_contract.h"
#include "phone_os/voice_assistant_service.h"

#include <cJSON.h>
#include <esp_codec_dev.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rodakos { class MusicPlayerService {}; }

namespace {
using namespace std::chrono_literals;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using rodakos::VoiceInboundEvent;
using rodakos::VoiceInboundEventType;
const std::string kInitialize = R"({"jsonrpc":"2.0","id":"init","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{}}})";
const std::string kList = R"({"jsonrpc":"2.0","id":"list","method":"tools/list","params":{}})";

bool WaitUntil(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

std::string Call(int id = 1) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
        ",\"method\":\"tools/call\",\"params\":{\"name\":\"self.audio_speaker.volume_up\","
        "\"arguments\":{},\"_meta\":{\"rodak/deviceEffect\":{\"effectId\":\"service-effect\","
        "\"parametersHash\":\"" + std::string(64, 'a') + "\"}}}}";
}

Json Parse(const std::string& value) {
    Json parsed(cJSON_Parse(value.c_str()), cJSON_Delete);
    RODAK_CHECK(parsed != nullptr);
    return parsed;
}
const cJSON* Get(const cJSON* value, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(value, key);
}
const cJSON* Receipt(const Json& value) {
    const cJSON* receipt = Get(Get(value.get(), "result"), "structuredContent");
    RODAK_CHECK(receipt != nullptr);
    return receipt;
}

class Recorder final : public rodakos::VoiceRecorderService {
public:
    bool Init() override { return true; }
    void Deinit() override { running = false; }
    bool Start(const rodakos::VoiceRecorderConfig&) override { running = true; return true; }
    void Stop() override { running = false; }
    bool IsRunning() const override { return running; }
    bool PopFrame(rodakos::VoicePcmFrame&) override { return false; }
    const char* name() const override { return "host-recorder"; }
    const char* last_error() const override { return "host recorder error"; }
private:
    std::atomic<bool> running{false};
};

class Transport final : public rodakos::VoiceAssistantTransport {
public:
    bool Start() override { return true; }
    bool OpenAudioChannel(rodakos::VoiceOpenGuard can_continue = {}) override {
        if (can_continue && !can_continue()) return false;
        ++generation;
        open = true;
        {
            std::lock_guard<std::mutex> lock(mutex);
            session_gate.Clear();
            session_gate.Establish(generation, SessionId(generation));
        }
        // 模拟 session.ready 到达后、OpenAudioChannel 尚未返回时的真实入站回调。
        if (initialize_on_open) Emit(kInitialize);
        if (on_open) on_open();
        return !can_continue || can_continue();
    }
    void CloseAudioChannel() override {
        open = false;
        std::lock_guard<std::mutex> lock(mutex);
        session_gate.Clear();
    }
    void WaitForAudioChannelClosed() override {}
    bool IsAudioChannelOpen() const override { return open; }
    uint32_t connection_generation() const override { return generation; }
    bool SendAudio(const rodakos::VoiceAudioPacket&, uint32_t expected) override {
        return Current(expected);
    }
    bool SendStartListening(rodakos::VoiceListeningMode, uint32_t expected) override {
        return Current(expected);
    }
    bool SendStopListening(uint32_t expected) override { return Current(expected); }
    bool SendWakeWordDetected(const std::string&, uint32_t expected) override {
        return Current(expected);
    }
    bool SendAbortSpeaking(rodakos::VoiceAbortReason, uint32_t expected, uint32_t) override {
        return Current(expected);
    }
    bool SendVadStart(const char*, uint32_t, uint32_t, uint32_t expected, uint32_t) override {
        return Current(expected);
    }
    bool SendVadEnd(const char*, uint32_t, uint32_t, uint32_t expected, uint32_t) override {
        return Current(expected);
    }
    bool SendMcpMessage(const std::string& payload, uint32_t expected) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (!Current(expected)) return false;
        const std::string wire = rodakos::BuildRealtimeVoiceMcpMessage(SessionId(expected), payload);
        Json envelope(cJSON_Parse(wire.c_str()), cJSON_Delete);
        VoiceInboundEvent decoded;
        std::string error;
        if (!rodakos::ParseRealtimeVoiceMcpInbound(
                envelope.get(), expected, session_gate, decoded, error)) return false;
        wire_responses.push_back(wire);
        responses.push_back(std::move(decoded.payload));
        changed.notify_all();
        return true;
    }
    void SetInboundHandler(rodakos::VoiceInboundHandler value) override {
        std::lock_guard<std::mutex> lock(mutex);
        handler = std::move(value);
    }
    void SetMcpEndpointAvailable(bool value) override { mcp_available = value; }
    const char* name() const override { return "host-transport"; }
    std::string last_error() const override { return "host transport error"; }
    rodakos::VoiceTransportFailure last_failure() const override { return {}; }

    void Emit(const std::string& payload, uint32_t expected = 0) {
        const uint32_t scope = expected == 0 ? generation.load() : expected;
        EmitEnvelope(rodakos::BuildRealtimeVoiceMcpMessage(SessionId(scope), payload), scope);
    }
    bool EmitEnvelope(const std::string& envelope, uint32_t expected = 0) {
        Json root(cJSON_Parse(envelope.c_str()), cJSON_Delete);
        VoiceInboundEvent event;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!rodakos::ParseRealtimeVoiceMcpInbound(root.get(),
                    expected == 0 ? generation.load() : expected, session_gate, event, error)) {
                return false;
            }
        }
        Deliver(std::move(event));
        return true;
    }
    // 已被旧 transport 捕获、延迟到新 scope 才送出的回调仍由真实 service 再次校验。
    void EmitCaptured(const std::string& payload, uint32_t expected) {
        VoiceInboundEvent event;
        event.type = VoiceInboundEventType::kMcp;
        event.transport_generation = expected;
        event.payload = payload;
        Deliver(std::move(event));
    }
    void FailConnection() {
        VoiceInboundEvent event;
        event.type = VoiceInboundEventType::kError;
        event.transport_generation = generation;
        event.failure.kind = rodakos::VoiceTransportFailureKind::kNetwork;
        event.failure.code = "test_disconnect";
        event.failure.message = "test disconnect";
        event.failure.retryable = true;
        event.failure.transport_generation = generation;
        open = false;
        Deliver(std::move(event));
    }
    size_t ResponseCount() {
        std::lock_guard<std::mutex> lock(mutex);
        return responses.size();
    }
    std::string WaitResponse(size_t index) {
        std::unique_lock<std::mutex> lock(mutex);
        RODAK_CHECK(changed.wait_for(lock, 3s, [this, index]() {
            return responses.size() > index;
        }));
        return responses[index];
    }
    std::string Exchange(const std::string& payload) {
        const size_t index = ResponseCount();
        Emit(payload);
        return WaitResponse(index);
    }
    std::string WireResponse(size_t index) {
        std::lock_guard<std::mutex> lock(mutex);
        return wire_responses.at(index);
    }

    std::atomic<bool> initialize_on_open{false};
    std::atomic<bool> mcp_available{false};
    std::function<void()> on_open;

private:
    static std::string SessionId(uint32_t value) {
        return "host-session-" + std::to_string(value);
    }
    bool Current(uint32_t expected) const { return open && expected == generation; }
    void Deliver(VoiceInboundEvent&& event) {
        rodakos::VoiceInboundHandler callback;
        {
            std::lock_guard<std::mutex> lock(mutex);
            callback = handler;
        }
        if (callback) callback(std::move(event));
    }
    std::atomic<bool> open{false};
    std::atomic<uint32_t> generation{0};
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> responses;
    std::vector<std::string> wire_responses;
    rodakos::VoiceInboundHandler handler;
    rodakos::RealtimeVoiceSessionGate session_gate;
};

struct Fixture {
    Fixture() { fake_codec::Reset(); }
    ~Fixture() {
        rodakos_test::ResumeWorkers();
        service.StopInteraction();
        rodakos_test::JoinWorkers();
    }
    bool Start() {
        return service.StartInteraction(rodakos::VoiceAssistantTrigger::kWakeWord, "你好达克");
    }
    void StartInitialized() {
        transport.initialize_on_open = true;
        RODAK_CHECK(Start());
        auto initialized = Parse(transport.WaitResponse(0));
        RODAK_CHECK(Get(initialized.get(), "result") != nullptr);
    }

    rodakos::MusicPlayerService music;
    rodakos::AudioOutputService output;
    rodakos::AudioFocusService focus{music, output};
    Transport transport;
    Recorder recorder;
    rodakos::VoiceAssistantService service{focus, transport, recorder, output};
};
}

RODAK_TEST("Voice service retains initialize received synchronously during channel open") {
    Fixture f;
    RODAK_CHECK(f.transport.mcp_available);
    f.StartInitialized();
    auto listed = Parse(f.transport.Exchange(kList));
    RODAK_CHECK_EQ(cJSON_GetArraySize(Get(Get(listed.get(), "result"), "tools")), 3);
    auto called = Parse(f.transport.Exchange(Call()));
    RODAK_CHECK_EQ(Get(Receipt(called), "volume")->valueint, 70);
    RODAK_CHECK_EQ(f.output.volume(), 70);
    RODAK_CHECK_EQ(fake_codec::opens, 0);
    RODAK_CHECK_EQ(fake_codec::volume_writes, 0);
}

RODAK_TEST("Voice service requires initialize and replays one committed relative operation") {
    Fixture f;
    RODAK_CHECK(f.Start());
    auto rejected = Parse(f.transport.Exchange(Call()));
    RODAK_CHECK(Get(rejected.get(), "error") != nullptr);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    f.transport.Exchange(kInitialize);
    const std::string accepted = f.transport.Exchange(Call());
    RODAK_CHECK_EQ(f.transport.Exchange(Call()), accepted);
    f.transport.Exchange(kInitialize);
    RODAK_CHECK_EQ(f.transport.Exchange(Call()), accepted);
    RODAK_CHECK_EQ(f.output.volume(), 70);
    auto parsed = Parse(accepted);
    RODAK_CHECK_EQ(Get(Receipt(parsed), "configurationRevision")->valueint, 1);
}

RODAK_TEST("Voice service drops stale generations and resets handshake and ledger after stop") {
    Fixture f;
    f.StartInitialized();
    f.transport.Exchange(Call());
    const uint32_t old_generation = f.transport.connection_generation();
    f.service.StopInteraction();
    const size_t stopped_responses = f.transport.ResponseCount();
    f.transport.EmitCaptured(Call(2), old_generation);
    RODAK_CHECK_EQ(f.transport.ResponseCount(), stopped_responses);
    RODAK_CHECK_EQ(f.output.volume(), 70);
    f.transport.initialize_on_open = false;
    RODAK_CHECK(f.Start());
    f.transport.EmitCaptured(Call(2), old_generation);
    auto rejected = Parse(f.transport.Exchange(Call()));
    RODAK_CHECK(Get(rejected.get(), "error") != nullptr);
    RODAK_CHECK_EQ(f.output.volume(), 70);
    f.transport.Exchange(kInitialize);
    auto accepted = Parse(f.transport.Exchange(Call()));
    RODAK_CHECK_EQ(Get(Receipt(accepted), "configurationRevision")->valueint, 2);
    RODAK_CHECK_EQ(f.output.volume(), 80);
}

RODAK_TEST("Voice service cancels queued MCP before stop releases its I/O task") {
    Fixture f;
    f.StartInitialized();
    RODAK_CHECK(rodakos_test::PauseWorkers());
    f.transport.Emit(Call());
    std::thread stopping([&]() { f.service.StopInteraction(); });
    const bool stop_started = WaitUntil([&]() { return f.service.GetState().stopping; });
    f.transport.Emit(Call(2));
    rodakos_test::ResumeWorkers();
    stopping.join();
    RODAK_CHECK(stop_started);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(f.transport.ResponseCount(), 1u);
    RODAK_CHECK_EQ(fake_codec::volume_writes, 0);
    RODAK_CHECK_EQ(f.service.GetState().phase, rodakos::VoiceAssistantPhase::kIdle);
}

RODAK_TEST("Voice service reconnect clears the ledger and requires a new MCP handshake") {
    Fixture f;
    f.StartInitialized();
    f.transport.Exchange(Call());
    const uint32_t old_generation = f.transport.connection_generation();
    f.transport.initialize_on_open = false;
    f.transport.FailConnection();
    RODAK_CHECK(WaitUntil([&]() {
        return f.transport.connection_generation() != old_generation &&
            f.service.GetState().transport_active;
    }));
    f.transport.EmitCaptured(Call(2), old_generation);
    auto rejected = Parse(f.transport.Exchange(Call()));
    RODAK_CHECK(Get(rejected.get(), "error") != nullptr);
    RODAK_CHECK_EQ(f.output.volume(), 70);
    f.transport.Exchange(kInitialize);
    auto accepted = Parse(f.transport.Exchange(Call()));
    RODAK_CHECK_EQ(Get(Receipt(accepted), "configurationRevision")->valueint, 2);
    RODAK_CHECK_EQ(f.output.volume(), 80);
}

RODAK_TEST("Voice service reconnect retains initialize arriving before the retry returns") {
    Fixture f;
    f.StartInitialized();
    f.transport.Exchange(Call());
    const size_t before = f.transport.ResponseCount();
    const uint32_t old_generation = f.transport.connection_generation();
    f.transport.FailConnection();
    auto initialized = Parse(f.transport.WaitResponse(before));
    RODAK_CHECK(Get(initialized.get(), "result") != nullptr);
    RODAK_CHECK_NE(f.transport.connection_generation(), old_generation);
    auto accepted = Parse(f.transport.Exchange(Call()));
    RODAK_CHECK_EQ(Get(Receipt(accepted), "configurationRevision")->valueint, 2);
    RODAK_CHECK_EQ(f.output.volume(), 80);
}

RODAK_TEST("Voice service keeps the startup MCP queue bounded without applying uninitialized tools") {
    Fixture f;
    f.transport.on_open = [&]() {
        f.transport.Emit(kInitialize);
        for (int index = 0; index < 64; ++index) f.transport.Emit(Call(index));
    };
    RODAK_CHECK(f.Start());
    auto last = Parse(f.transport.WaitResponse(63));
    RODAK_CHECK(Get(last.get(), "error") != nullptr);
    f.service.StopInteraction();
    RODAK_CHECK_EQ(f.transport.ResponseCount(), 64u);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(fake_codec::volume_writes, 0);
}

RODAK_TEST("Voice service stop during channel open discards early initialize and tools") {
    Fixture f;
    std::mutex gate;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    f.transport.on_open = [&]() {
        f.transport.Emit(kInitialize);
        f.transport.Emit(Call());
        std::unique_lock<std::mutex> lock(gate);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&]() { return release; });
    };
    bool started = true;
    std::thread starting([&]() { started = f.Start(); });
    bool open_entered = false;
    {
        std::unique_lock<std::mutex> lock(gate);
        open_entered = changed.wait_for(lock, 3s, [&]() { return entered; });
    }
    std::thread stopping([&]() { f.service.StopInteraction(); });
    const bool stop_started = WaitUntil([&]() { return f.service.GetState().stopping; });
    {
        std::lock_guard<std::mutex> lock(gate);
        release = true;
        changed.notify_all();
    }
    starting.join();
    stopping.join();
    RODAK_CHECK(open_entered);
    RODAK_CHECK(stop_started);
    RODAK_CHECK_FALSE(started);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(f.transport.ResponseCount(), 0u);
    RODAK_CHECK_EQ(fake_codec::volume_writes, 0);
}

RODAK_TEST("Voice service consumes canonical MCP envelopes only through a matching adapter gate") {
    Fixture f;
    f.StartInitialized();
    const uint32_t generation = f.transport.connection_generation();
    const size_t before = f.transport.ResponseCount();
    const std::string correct = rodakos::BuildRealtimeVoiceMcpMessage(
        "host-session-" + std::to_string(generation), Call());
    RODAK_CHECK_FALSE(f.transport.EmitEnvelope(correct, generation + 1));
    RODAK_CHECK_FALSE(f.transport.EmitEnvelope(
        rodakos::BuildRealtimeVoiceMcpMessage("wrong-session", Call())));
    RODAK_CHECK_FALSE(f.transport.EmitEnvelope(
        R"({"event":"mcp","sessionId":"host-session-1","payload":[]})"));
    RODAK_CHECK_EQ(f.transport.ResponseCount(), before);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK(f.transport.EmitEnvelope(correct));
    auto accepted = Parse(f.transport.WaitResponse(before));
    RODAK_CHECK_EQ(Get(Receipt(accepted), "configurationRevision")->valueint, 1);
    RODAK_CHECK_EQ(f.output.volume(), 70);
}

RODAK_TEST("Voice service round trips typed RPC IDs and receipts through canonical response envelopes") {
    Fixture f;
    f.StartInitialized();
    f.transport.Exchange(Call());
    std::string string_id_call = Call();
    const size_t position = string_id_call.find("\"id\":1");
    RODAK_CHECK(position != std::string::npos);
    string_id_call.replace(position, 6, "\"id\":\"1\"");
    f.transport.Exchange(string_id_call);
    auto numeric = Parse(f.transport.WireResponse(1));
    auto text = Parse(f.transport.WireResponse(2));
    RODAK_CHECK_EQ(std::string(Get(numeric.get(), "event")->valuestring), "mcp");
    RODAK_CHECK_EQ(std::string(Get(numeric.get(), "sessionId")->valuestring), "host-session-1");
    const cJSON* numeric_rpc = Get(numeric.get(), "payload");
    const cJSON* text_rpc = Get(text.get(), "payload");
    RODAK_CHECK(cJSON_IsNumber(Get(numeric_rpc, "id")));
    RODAK_CHECK(cJSON_IsString(Get(text_rpc, "id")));
    const cJSON* numeric_receipt = Get(Get(numeric_rpc, "result"), "structuredContent");
    const cJSON* text_receipt = Get(Get(text_rpc, "result"), "structuredContent");
    RODAK_CHECK(cJSON_Compare(numeric_receipt, text_receipt, true));
    RODAK_CHECK_EQ(std::string(Get(text_receipt, "effectId")->valuestring), "service-effect");
    RODAK_CHECK_EQ(Get(text_receipt, "configurationRevision")->valueint, 1);
    RODAK_CHECK_EQ(f.output.volume(), 70);
}
