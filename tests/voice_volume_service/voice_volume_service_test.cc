#include "host_runtime.h"
#include "allocation_probe.h"
#include "delayed_recording_failure.h"
#include "test_framework.h"
#include "task_retirement_host.h"
#include "phone_os/task-retirement.h"
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


class Recorder final : public rodakos::VoiceRecorderService {
public:
    bool Init() override { return true; }
    void Deinit() override { running = false; if (on_deinit) on_deinit(); }
    bool Start(const rodakos::VoiceRecorderConfig&) override { running = true; return true; }
    void Stop() override { running = false; }
    bool IsRunning() const override {
        const bool snapshot = running;
        if (on_running_snapshot) on_running_snapshot();
        return snapshot;
    }
    bool PopFrame(rodakos::VoicePcmFrame&) override { ++empty_polls; return false; }
    const char* name() const override { return "esp-sr-multinet"; }
    const char* last_error() const override { return "host recorder error"; }
    std::function<void()> on_deinit;
    std::function<void()> on_running_snapshot;
    std::atomic<unsigned> empty_polls{0};
    void FailCapture() { running = false; }
private:
    std::atomic<bool> running{false};
};

class Transport final : public rodakos::VoiceAssistantTransport {
public:
    bool Start() override { return true; }
    bool PrepareInteraction(rodakos::VoiceOpenGuard can_continue = {}) override {
        ++prepare_calls;
        if (on_prepare) on_prepare();
        if (can_continue && !can_continue()) return false;
        return prepare_ok;
    }
    bool OpenAudioChannel(rodakos::VoiceOpenGuard can_continue = {}) override {
        if (can_continue && !can_continue()) return false;
        ++generation;
        open = true;
        {
            std::lock_guard<std::mutex> lock(mutex);
            session_gate.Clear();
            session_gate.Establish(generation, SessionId(generation));
        }
        if (on_open) on_open();
        return !can_continue || can_continue();
    }
    void CloseAudioChannel() override {
        if (on_close) on_close();
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
    const char* name() const override { return "rodak-realtime-voice"; }
    std::string last_error() const override { return "host transport error"; }
    rodakos::VoiceTransportFailure last_failure() const override { return failure; }

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
    void EndSession() {
        VoiceInboundEvent event;
        event.type = VoiceInboundEventType::kSessionFinished;
        event.transport_generation = generation;
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

    std::function<void()> on_open;
    std::function<void()> on_prepare;
    std::function<void()> on_close;
    bool prepare_ok = true;
    unsigned prepare_calls = 0;
    rodakos::VoiceTransportFailure failure;

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
    Fixture() { fake_codec::Reset(); rodakos_test::ResetWorkers(); }
    ~Fixture() {
        rodakos_test::SetAfterSemaphoreGiveHook({});
        rodakos_test::ResumeWorkers();
        service.StopInteraction();
        rodakos_test::JoinWorkers();
    }
    bool Start() {
        return service.StartInteraction(rodakos::VoiceAssistantTrigger::kWakeWord, "你好达克");
    }
    void StartInitialized() {
        RODAK_CHECK(Start());
        RODAK_CHECK(WaitUntil([&]() { return service.GetState().transport_active; }));
    }

    rodakos::MusicPlayerService music;
    rodakos::AudioOutputService output;
    rodakos::AudioFocusService focus{music, output};
    Transport transport;
    Recorder recorder;
    rodakos::VoiceAssistantService service{focus, transport, recorder, output};
};
}

#ifndef RODAK_ASSISTANT_LEGACY_BASELINE
RODAK_TEST("phase snapshot follows real assistant phases without allocating diagnostic strings") {
    Fixture f;
    const std::string long_message(1024, 'm');
    const auto check_phase = [&](rodakos::VoiceAssistantPhase expected) {
        rodakos_test::BeginAllocationProbe();
        const auto phase = f.service.GetPhaseSnapshot();
        const auto narrow = rodakos_test::EndAllocationProbe();
        RODAK_CHECK_EQ(phase, expected);
        RODAK_CHECK_EQ(narrow.count, 0u);
        rodakos_test::BeginAllocationProbe();
        const auto full = f.service.GetState();
        const auto copied = rodakos_test::EndAllocationProbe();
        RODAK_CHECK_EQ(full.phase, phase);
        RODAK_CHECK(copied.count > 0);
        if (expected != rodakos::VoiceAssistantPhase::kIdle) {
            RODAK_CHECK_EQ(full.message, long_message);
            RODAK_CHECK(copied.bytes >= long_message.size() + 1);
        }
    };
    RODAK_CHECK_FALSE(f.service.GetState().initialized);
    check_phase(rodakos::VoiceAssistantPhase::kIdle);
    RODAK_CHECK(f.service.Init());
    check_phase(rodakos::VoiceAssistantPhase::kIdle);
    f.service.MarkConnecting(long_message.c_str());
    check_phase(rodakos::VoiceAssistantPhase::kConnecting);
    f.service.MarkListening(long_message.c_str());
    check_phase(rodakos::VoiceAssistantPhase::kListening);
    f.service.MarkSpeaking(long_message.c_str());
    check_phase(rodakos::VoiceAssistantPhase::kSpeaking);
    f.service.MarkError(long_message);
    check_phase(rodakos::VoiceAssistantPhase::kError);
    f.service.Deinit();
    check_phase(rodakos::VoiceAssistantPhase::kIdle);
}

RODAK_TEST("empty capture queue keeps the real assistant listening while recorder runs") {
    Fixture f;
    f.StartInitialized();
    const auto before = f.recorder.empty_polls.load();
    RODAK_CHECK(WaitUntil([&] { return f.recorder.empty_polls.load() >= before + 3; }));
    const auto state = f.service.GetState();
    RODAK_CHECK_EQ(state.phase, rodakos::VoiceAssistantPhase::kListening);
    RODAK_CHECK(state.recorder_active && state.transport_active && state.focus_active);
    RODAK_CHECK_FALSE(state.stopping);
}

RODAK_TEST("terminal capture failure exits real listening and speaking then permits restart") {
    for (const bool speaking : {false, true}) {
        Fixture f;
        f.StartInitialized();
        if (speaking) f.service.MarkSpeaking("Speaking");
        f.recorder.FailCapture();
        const bool failed = WaitUntil([&] {
            const auto state = f.service.GetState();
            return state.phase == rodakos::VoiceAssistantPhase::kError && !state.stopping;
        });
        const auto state = f.service.GetState();
        RODAK_CHECK(failed);
        RODAK_CHECK_EQ(state.message, "Voice capture stopped unexpectedly");
        RODAK_CHECK_EQ(state.diagnostic, rodakos::CloudDiagnosticCode::kVoiceUnavailable);
        RODAK_CHECK_FALSE(state.recorder_active || state.transport_active || state.focus_active);
        RODAK_CHECK_FALSE(f.transport.IsAudioChannelOpen());
        RODAK_CHECK(f.Start());
        RODAK_CHECK_EQ(f.service.GetPhaseSnapshot(), rodakos::VoiceAssistantPhase::kListening);
    }
}

RODAK_TEST("normal Stop wins over a delayed terminal recorder observation") {
    Fixture f;
    retirement_host::Gate observation;
    std::atomic<bool> armed{false};
    f.recorder.on_running_snapshot = [&] {
        if (retirement_host::IsWorkerTask() && armed.exchange(false)) observation.Enter();
    };
    f.StartInitialized();
    RODAK_CHECK(rodakos_test::PauseWorkers());
    f.recorder.FailCapture();
    armed = true;
    rodakos_test::ResumeWorkers();
    const bool observed = observation.Wait();
    std::thread stop([&] { f.service.StopInteraction(); });
    const bool stopping = WaitUntil([&] { return f.service.GetState().stopping; });
    observation.Release();
    stop.join();
    f.recorder.on_running_snapshot = {};
    const auto state = f.service.GetState();
    RODAK_CHECK(observed && stopping);
    RODAK_CHECK_EQ(state.phase, rodakos::VoiceAssistantPhase::kIdle);
    RODAK_CHECK_EQ(state.message, "Ready");
    RODAK_CHECK_FALSE(state.stopping || state.focus_active || state.recorder_active);
}

RODAK_TEST("delayed capture failure endpoint rejects an old transport after real reconnect") {
    Fixture f;
    uint32_t interaction = 0;
    RODAK_CHECK(f.service.StartInteraction(
        rodakos::VoiceAssistantTrigger::kWakeWord, "", {}, &interaction));
    const auto old_transport = f.transport.connection_generation();
    f.transport.FailConnection();
    RODAK_CHECK(WaitUntil([&] {
        return f.transport.connection_generation() != old_transport &&
               f.service.GetPhaseSnapshot() == rodakos::VoiceAssistantPhase::kListening;
    }));
    uint32_t unchanged_interaction = 0;
    RODAK_CHECK(f.service.StartInteraction(
        rodakos::VoiceAssistantTrigger::kWakeWord, "", {}, &unchanged_interaction));
    RODAK_CHECK_EQ(unchanged_interaction, interaction);
    rodakos_test::SubmitDelayedRecordingFailure(f.service, interaction, old_transport);
    const auto state = f.service.GetState();
    RODAK_CHECK_EQ(state.phase, rodakos::VoiceAssistantPhase::kListening);
    RODAK_CHECK(state.transport_active && state.recorder_active && state.focus_active);
    RODAK_CHECK_FALSE(state.stopping);
}

RODAK_TEST("delayed capture failure endpoint rejects an old interaction after restart") {
    Fixture f;
    uint32_t old_interaction = 0, new_interaction = 0;
    RODAK_CHECK(f.service.StartInteraction(
        rodakos::VoiceAssistantTrigger::kWakeWord, "", {}, &old_interaction));
    const auto old_transport = f.transport.connection_generation();
    f.service.StopInteraction();
    RODAK_CHECK(f.service.StartInteraction(
        rodakos::VoiceAssistantTrigger::kWakeWord, "", {}, &new_interaction));
    RODAK_CHECK_NE(old_interaction, new_interaction);
    rodakos_test::SubmitDelayedRecordingFailure(f.service, old_interaction, old_transport);
    const auto state = f.service.GetState();
    RODAK_CHECK_EQ(state.phase, rodakos::VoiceAssistantPhase::kListening);
    RODAK_CHECK(state.transport_active && state.recorder_active && state.focus_active);
    RODAK_CHECK_FALSE(state.stopping);
}

#endif

RODAK_TEST("Voice service ignores MCP messages and never changes volume through them") {
    Fixture f;
    f.transport.on_open = [&]() {
        f.transport.Emit(kInitialize);
        f.transport.Emit(Call());
    };
    f.StartInitialized();
    f.transport.Emit(kInitialize);
    f.transport.Emit(Call(2));
    // 入站事件按序处理：会话结束生效时，前面的 MCP 消息必然已经处理完。
    f.transport.EndSession();
    RODAK_CHECK(WaitUntil([&]() {
        const auto state = f.service.GetState();
        return state.phase == rodakos::VoiceAssistantPhase::kIdle && !state.stopping;
    }));
    RODAK_CHECK_EQ(f.transport.ResponseCount(), 0u);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(fake_codec::volume_writes, 0);
}

RODAK_TEST("Voice diagnostics distinguish preparation failures without exposing backend messages") {
    using rodakos::CloudDiagnosticCode;
    for (auto code : {CloudDiagnosticCode::kUnconfigured, CloudDiagnosticCode::kCredentialsRejected,
                      CloudDiagnosticCode::kCredentialsExpired, CloudDiagnosticCode::kRefreshFailed,
                      CloudDiagnosticCode::kNetworkUnavailable, CloudDiagnosticCode::kVoiceUnavailable}) {
        Fixture f;
        RODAK_CHECK(f.service.Init());
        RODAK_CHECK_EQ(f.transport.prepare_calls, 0U);
        RODAK_CHECK_EQ(f.transport.connection_generation(), 0U);
        f.transport.prepare_ok = false;
        f.transport.failure = {rodakos::VoiceTransportFailureKind::kConfiguration,
            "credential_refresh_failed", "raw-secret-token-response", false, 0, code};
        RODAK_CHECK_FALSE(f.Start());
        const auto state = f.service.GetState();
        RODAK_CHECK_EQ(state.phase, rodakos::VoiceAssistantPhase::kError);
        RODAK_CHECK_EQ(state.diagnostic, code);
        RODAK_CHECK_EQ(state.message, rodakos::CloudDiagnosticTitle(code));
        RODAK_CHECK_EQ(f.transport.connection_generation(), 0U);
        RODAK_CHECK_FALSE(f.recorder.IsRunning());
        f.transport.prepare_ok = true;
        RODAK_CHECK(f.Start());
        RODAK_CHECK_EQ(f.service.GetState().phase, rodakos::VoiceAssistantPhase::kListening);
        RODAK_CHECK_EQ(f.service.GetState().diagnostic, CloudDiagnosticCode::kReady);
    }
}

RODAK_TEST("Stopping during credential preparation cannot publish a stale error or open voice") {
    Fixture f;
    f.transport.on_prepare = [&] { f.service.StopInteraction(); };
    f.transport.failure = {rodakos::VoiceTransportFailureKind::kCancelled,
                          "prepare_cancelled", "raw-secret-token-response", false};
    RODAK_CHECK_FALSE(f.Start());
    RODAK_CHECK_EQ(f.service.GetState().phase, rodakos::VoiceAssistantPhase::kIdle);
    RODAK_CHECK_EQ(f.transport.connection_generation(), 0U);
    RODAK_CHECK_FALSE(f.recorder.IsRunning());
    f.transport.on_prepare = {};
    RODAK_CHECK(f.Start());
}

RODAK_TEST("before-transport callback runs after capture starts and before credential preparation") {
    Fixture f;
    std::vector<std::string> order;
    f.service.SetBeforeTransportCallback([&] {
        order.push_back(f.recorder.IsRunning() ? "before:recording" : "before:idle");
    });
    f.transport.on_prepare = [&] { order.push_back("prepare"); };
    RODAK_CHECK(f.Start());
    RODAK_CHECK_EQ(order.size(), 2U);
    RODAK_CHECK_EQ(order[0], std::string("before:recording"));
    RODAK_CHECK_EQ(order[1], std::string("prepare"));
    f.service.StopInteraction();

    // A Stop that lands while the callback runs (Camera teardown can take ~300 ms)
    // must keep the transport closed.
    order.clear();
    f.service.SetBeforeTransportCallback([&] { f.service.StopInteraction(); });
    RODAK_CHECK_FALSE(f.Start());
    RODAK_CHECK(order.empty());
    RODAK_CHECK_EQ(f.service.GetState().phase, rodakos::VoiceAssistantPhase::kIdle);
    RODAK_CHECK_FALSE(f.recorder.IsRunning());
}

RODAK_TEST("retirement assistant exits without allocating an IDF cleanup task") {
    Fixture f;
    f.StartInitialized();
    const auto before = retirement_host::Snapshot();
    std::cout << "ASSISTANT_IO_STARTED_REAL_SERVICE" << std::endl;
    f.service.StopInteraction();
    const auto after = retirement_host::Snapshot();
    RODAK_CHECK_EQ(after.cleanup_create_attempts, 0u);
    RODAK_CHECK_EQ(after.live_tasks, 0u);
    RODAK_CHECK_EQ(after.live_task_buffers, 0u);
    RODAK_CHECK_EQ(after.task_deletes, 1u);
    RODAK_CHECK_EQ(after.allocation_calls, before.allocation_calls);
}

namespace {
std::atomic<bool> publication_observed{false};
void ObserveUnpublishedAssistant(TaskHandle_t) { publication_observed = true; }

void ParkAfterNativeIdle(Fixture& f, retirement_host::Gate& gate,
                         std::atomic<bool>& armed) {
    rodakos_test::SetAfterSemaphoreGiveHook([&] {
        if (!retirement_host::IsWorkerTask() || !armed.load()) return;
        const auto state = f.service.GetState();
        if (state.phase == rodakos::VoiceAssistantPhase::kIdle && !state.stopping &&
            armed.exchange(false)) gate.Enter();
    });
}
}

RODAK_TEST("retirement assistant publishes before the scheduled worker enters its body") {
    Fixture f;
    publication_observed = false;
    retirement_host::SetBeforeCreateReturnsHook(ObserveUnpublishedAssistant);
    const bool started = f.Start();
    retirement_host::SetBeforeCreateReturnsHook(nullptr);
    f.service.StopInteraction();
    RODAK_CHECK(started);
    RODAK_CHECK(publication_observed.load());
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
}

RODAK_TEST("retirement assistant late idle Stop waits for the complete old body") {
    Fixture f;
    f.StartInitialized();
    retirement_host::Gate tail;
    std::atomic<bool> armed{true}, returned{false};
    ParkAfterNativeIdle(f, tail, armed);
    f.transport.EndSession();
    const bool reached = tail.Wait();
    std::thread stopper([&] { f.service.StopInteraction(); returned = true; });
    std::this_thread::sleep_for(40ms);
    const bool returned_early = returned.load();
    const size_t deleted_early = retirement_host::Snapshot().task_deletes;
    tail.Release();
    stopper.join();
    rodakos_test::SetAfterSemaphoreGiveHook({});
    RODAK_CHECK(reached);
    RODAK_CHECK_FALSE(returned_early);
    RODAK_CHECK_EQ(deleted_early, 0u);
    RODAK_CHECK(returned.load());
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
}

RODAK_TEST("retirement assistant failed replacement preserves the previous ticket") {
    Fixture f;
    f.StartInitialized();
    retirement_host::Gate tail;
    std::atomic<bool> armed{true}, returned{false}, accepted{true};
    ParkAfterNativeIdle(f, tail, armed);
    f.transport.EndSession();
    const bool reached = tail.Wait();
    retirement_host::SetCreationAllowed(false);
    std::thread starter([&] { accepted = f.Start(); returned = true; });
    std::this_thread::sleep_for(40ms);
    const bool returned_early = returned.load();
    retirement_host::SetCreationAllowed(true);
    tail.Release();
    starter.join();
    rodakos_test::SetAfterSemaphoreGiveHook({});
    const auto reclaimed = retirement_host::Snapshot();
    const bool retried = f.Start();
    f.service.StopInteraction();
    RODAK_CHECK(reached);
    RODAK_CHECK_FALSE(accepted.load());
    RODAK_CHECK_FALSE(returned_early);
    RODAK_CHECK_EQ(reclaimed.task_deletes, 1u);
    RODAK_CHECK_EQ(reclaimed.live_task_buffers, 0u);
    RODAK_CHECK(retried);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 2u);
}

RODAK_TEST("retirement assistant autonomous session end is reaped without external Stop") {
    Fixture f;
    f.StartInitialized();
    f.transport.EndSession();
    const bool idle = WaitUntil([&] {
        const auto state = f.service.GetState();
        return state.phase == rodakos::VoiceAssistantPhase::kIdle && !state.stopping;
    });
    const bool reclaimed = WaitUntil([&] {
        rodakos::PumpTaskRetirements();
        return retirement_host::Snapshot().task_deletes == 1;
    });
    RODAK_CHECK(idle);
    RODAK_CHECK(reclaimed);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().cleanup_create_attempts, 0u);
}

RODAK_TEST("retirement assistant concurrent Stops and pump reclaim one old task") {
    Fixture f;
    f.StartInitialized();
    retirement_host::Gate tail;
    std::atomic<bool> armed{true}, first_done{false}, second_done{false}, pump_running{true};
    ParkAfterNativeIdle(f, tail, armed);
    f.transport.EndSession();
    const bool reached = tail.Wait();
    std::thread first([&] { f.service.StopInteraction(); first_done = true; });
    const bool stopping = WaitUntil([&] { return f.service.GetState().stopping; });
    std::thread second([&] { f.service.StopInteraction(); second_done = true; });
    std::thread pump([&] {
        while (pump_running) { rodakos::PumpTaskRetirements(); std::this_thread::sleep_for(1ms); }
    });
    std::this_thread::sleep_for(40ms);
    const bool returned_early = first_done || second_done;
    tail.Release();
    first.join();
    second.join();
    pump_running = false;
    pump.join();
    rodakos_test::SetAfterSemaphoreGiveHook({});
    RODAK_CHECK(reached && stopping);
    RODAK_CHECK_FALSE(returned_early);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
}

RODAK_TEST("retirement assistant Deinit preserves later Init and Start admission") {
    Fixture f;
    f.StartInitialized();
    f.service.Deinit();
    const auto first = retirement_host::Snapshot();
    RODAK_CHECK_FALSE(f.service.GetState().initialized);
    const bool initialized = f.service.Init();
    const bool restarted = f.Start();
    f.service.Deinit();
    RODAK_CHECK_EQ(first.live_tasks, 0u);
    RODAK_CHECK_EQ(first.live_task_buffers, 0u);
    RODAK_CHECK(initialized && restarted);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 2u);
}

RODAK_TEST("retirement assistant cleanup waiter does not follow a replacement cleanup") {
    Fixture f;
    f.StartInitialized();
    retirement_host::Gate replacement_close;
    std::atomic<TaskHandle_t> first_identity{nullptr};
    std::atomic<bool> replaced{false}, first_done{false}, replacement_started{false};
    std::thread replacement_stopper;
    rodakos_test::SetAfterSemaphoreGiveHook([&] {
        if (retirement_host::IsWorkerTask() ||
            xTaskGetCurrentTaskHandle() != first_identity.load() || replaced.load()) return;
        const auto state = f.service.GetState();
        if (state.phase != rodakos::VoiceAssistantPhase::kIdle || state.stopping ||
            replaced.exchange(true)) return;
        replacement_started = f.Start();
        f.transport.on_close = [&] { replacement_close.Enter(); };
        replacement_stopper = std::thread([&] { f.service.StopInteraction(); });
        replacement_close.Wait();
    });
    std::thread first([&] {
        first_identity = xTaskGetCurrentTaskHandle();
        f.service.StopInteraction();
        first_done = true;
    });
    const bool reached = replacement_close.Wait();
    const bool old_only = WaitUntil([&] { return first_done.load(); });
    replacement_close.Release();
    first.join();
    if (replacement_stopper.joinable()) replacement_stopper.join();
    f.transport.on_close = {};
    rodakos_test::SetAfterSemaphoreGiveHook({});
    RODAK_CHECK(reached && replacement_started);
    RODAK_CHECK(old_only);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 2u);
}

RODAK_TEST("retirement assistant Deinit waiter stays with its captured generation") {
    Fixture f;
    f.StartInitialized();
    retirement_host::Gate first_recorder, waiter_admitted, replacement_recorder;
    std::atomic<TaskHandle_t> waiter_identity{nullptr};
    std::atomic<bool> waiter_armed{true}, waiter_done{false};
    f.recorder.on_deinit = [&] { first_recorder.Enter(); };
    std::thread first([&] { f.service.Deinit(); });
    const bool first_reached = first_recorder.Wait();
    rodakos_test::SetAfterSemaphoreGiveHook([&] {
        if (xTaskGetCurrentTaskHandle() == waiter_identity.load() &&
            waiter_armed.exchange(false)) waiter_admitted.Enter();
    });
    std::thread waiter([&] {
        waiter_identity = xTaskGetCurrentTaskHandle();
        f.service.Deinit();
        waiter_done = true;
    });
    const bool waiter_reached = waiter_admitted.Wait();
    first_recorder.Release();
    first.join();
    const bool initialized = f.service.Init();
    const bool restarted = f.Start();
    f.recorder.on_deinit = [&] { replacement_recorder.Enter(); };
    std::thread replacement([&] { f.service.Deinit(); });
    const bool replacement_reached = replacement_recorder.Wait();
    waiter_admitted.Release();
    const bool old_only = WaitUntil([&] { return waiter_done.load(); });
    replacement_recorder.Release();
    replacement.join();
    waiter.join();
    f.recorder.on_deinit = {};
    rodakos_test::SetAfterSemaphoreGiveHook({});
    RODAK_CHECK(first_reached && waiter_reached && initialized && restarted && replacement_reached);
    RODAK_CHECK(old_only);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 2u);
}

RODAK_TEST("retirement assistant destructor closes admission before Deinit completes") {
    Fixture f;
    auto service = std::make_unique<rodakos::VoiceAssistantService>(
        f.focus, f.transport, f.recorder, f.output);
    auto* active = service.get();
    const bool started = active->StartInteraction(rodakos::VoiceAssistantTrigger::kWakeWord);
    std::atomic<bool> recorder_deinitialized{false}, admission_checked{false}, accepted{false};
    f.recorder.on_deinit = [&] { recorder_deinitialized = true; };
    rodakos_test::SetAfterSemaphoreGiveHook([&] {
        if (retirement_host::IsWorkerTask() || !recorder_deinitialized || admission_checked.exchange(true)) return;
        accepted = active->StartInteraction(rodakos::VoiceAssistantTrigger::kWakeWord);
        if (accepted) active->StopInteraction();
    });
    service.reset();
    rodakos_test::SetAfterSemaphoreGiveHook({});
    f.recorder.on_deinit = {};
    RODAK_CHECK(started);
    RODAK_CHECK(admission_checked.load());
    RODAK_CHECK_FALSE(accepted.load());
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_tasks, 0u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
}

RODAK_TEST("retirement assistant stale generation Stop does not wait for the replacement") {
    Fixture f;
    uint32_t first_generation = 0, second_generation = 0;
    RODAK_CHECK(f.service.StartInteraction(rodakos::VoiceAssistantTrigger::kWakeWord, "", {}, &first_generation));
    f.service.StopInteraction();
    RODAK_CHECK(f.service.StartInteraction(rodakos::VoiceAssistantTrigger::kWakeWord, "", {}, &second_generation));
    f.service.StopInteractionIfCurrent(first_generation);
    const auto state = f.service.GetState();
    const auto before = retirement_host::Snapshot();
    f.service.StopInteractionIfCurrent(second_generation);
    RODAK_CHECK(first_generation != second_generation);
    RODAK_CHECK(state.transport_active && !state.stopping);
    RODAK_CHECK_EQ(before.live_tasks, 1u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 2u);
}

RODAK_TEST("retirement assistant stopping waiter joins only its captured old task") {
    Fixture f;
    f.StartInitialized();
    retirement_host::Gate self_cleanup, waiter_admitted;
    std::atomic<TaskHandle_t> waiter_identity{nullptr};
    std::atomic<bool> replacement_attempted{false}, replacement_started{false}, waiter_done{false}, admitted{false};
    f.transport.on_close = [&] {
        if (retirement_host::IsWorkerTask()) self_cleanup.Enter();
    };
    f.transport.EndSession();
    const bool reached = self_cleanup.Wait();
    rodakos_test::SetAfterSemaphoreGiveHook([&] {
        if (retirement_host::IsWorkerTask() ||
            xTaskGetCurrentTaskHandle() != waiter_identity.load() || replacement_attempted.load()) return;
        const auto state = f.service.GetState();
        if (state.stopping && !admitted.exchange(true)) waiter_admitted.Enter();
        if (state.phase == rodakos::VoiceAssistantPhase::kIdle && !state.stopping &&
            !replacement_attempted.exchange(true)) replacement_started = f.Start();
    });
    std::thread waiter([&] {
        waiter_identity = xTaskGetCurrentTaskHandle();
        f.service.StopInteraction();
        waiter_done = true;
    });
    const bool waiter_reached = waiter_admitted.Wait();
    self_cleanup.Release();
    waiter_admitted.Release();
    const bool old_only = WaitUntil([&] { return waiter_done.load(); });
    f.service.StopInteraction();
    waiter.join();
    f.transport.on_close = {};
    rodakos_test::SetAfterSemaphoreGiveHook({});
    RODAK_CHECK(reached && waiter_reached && replacement_started);
    RODAK_CHECK(old_only);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 2u);
}
