#include "phone_os/voice_lifecycle_diagnostic.h"
#include "test_framework.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <limits>
#include <string>
#include <thread>

namespace {
using namespace rodakos;
using Phase = VoiceLifecycleAssistantPhase;
class Gate {
public:
    void Enter() {
        std::unique_lock<std::mutex> lock(mutex_);
        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] { return released_; });
    }
    bool Wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(3), [&] { return entered_; });
    }
    void Release() {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_ = false, released_ = false;
};

struct Fixture {
    std::mutex events_mutex;
    std::vector<VoiceLifecycleEvent> events;
    std::atomic<unsigned> deinit_calls{0}, restart_calls{0}, state_calls{0}, snapshot_calls{0};
    std::atomic<const char*> busy{nullptr};
    VoiceLifecycleWakeState before_wake{true, false}, after_wake{true, true};
    VoiceLifecycleSnapshot before{{true, true, true}, Phase::kListening};
    VoiceLifecycleSnapshot after{{false, false, false}, Phase::kIdle};
    VoiceLifecycleSnapshot restored{{false, true, true}, Phase::kIdle};
    bool restart_result = true;
    bool print = false;
    std::function<void()> deinit_hook;
    std::function<void()> busy_hook;
    std::function<void(const VoiceLifecycleEvent&)> emit_hook;
    VoiceLifecycleDiagnostic diagnostic;

    Fixture() : diagnostic({
        .context = this,
        .busy_reason = [](void* p) {
            auto& f = *static_cast<Fixture*>(p);
            if (f.busy_hook) f.busy_hook();
            return f.busy.load();
        },
        .snapshot = [](void* p) {
            auto& f = *static_cast<Fixture*>(p);
            const auto call = f.snapshot_calls.fetch_add(1);
            return call % 3 == 0 ? f.before : call % 3 == 1 ? f.after : f.restored;
        },
        .wake_state = [](void* p) {
            auto& f = *static_cast<Fixture*>(p);
            return f.state_calls.fetch_add(1) % 2 == 0 ? f.before_wake : f.after_wake;
        },
        .deinit = [](void* p) {
            auto& f = *static_cast<Fixture*>(p);
            ++f.deinit_calls;
            if (f.deinit_hook) f.deinit_hook();
        },
        .restart = [](void* p) {
            auto& f = *static_cast<Fixture*>(p);
            ++f.restart_calls;
            return f.restart_result;
        },
        .emit = [](void* p, const VoiceLifecycleEvent& event) {
            auto& f = *static_cast<Fixture*>(p);
            { std::lock_guard<std::mutex> lock(f.events_mutex); f.events.push_back(event); }
            if (f.print) PrintVoiceLifecycleEvent(nullptr, event);
            if (f.emit_hook) f.emit_hook(event);
        }
    }) {}
    void Queue(const std::string& id = "1") {
        RODAK_CHECK(diagnostic.HandleSerialLine("RODAK_RELEASE_TEST_V1 voice_cycle " + id));
    }
    VoiceLifecycleEvent Last() {
        std::lock_guard<std::mutex> lock(events_mutex);
        return events.back();
    }
    std::string Result() { return Last().result; }
};
}

RODAK_TEST("strict positive uint32 parser rejects signs whitespace suffixes zero overflow and duplicates") {
    Fixture f;
    RODAK_CHECK_FALSE(f.diagnostic.HandleSerialLine("RODAK_VOICE_TEST_V1 wake"));
    for (const auto* invalid : {"", "0", "01", "+1", "-1", " 1", "1 ", "1\t", "1 2", "1x", "4294967296", "99999999999"}) {
        f.Queue(invalid);
        RODAK_CHECK_EQ(f.Result(), "rejected");
        RODAK_CHECK_EQ(std::string(f.Last().reason), "invalid_id");
        RODAK_CHECK_FALSE(f.diagnostic.IsBusy());
    }
    f.Queue("4294967295");
    RODAK_CHECK(f.diagnostic.Pump());
    RODAK_CHECK_EQ(f.Last().id, std::numeric_limits<uint32_t>::max());
    f.Queue("4294967295");
    RODAK_CHECK_EQ(std::string(f.Last().reason), "stale_id");
    f.Queue("1");
    RODAK_CHECK_EQ(std::string(f.Last().reason), "stale_id");
    RODAK_CHECK_EQ(f.deinit_calls.load(), 1u);
}

RODAK_TEST("resource busy admission changes no voice state and rejected id can be reused") {
    Fixture f;
    for (const auto* reason : {"ota_busy", "camera_busy", "display_busy", "recording_busy", "music_busy", "audio_focus_busy"}) {
        f.busy = reason; f.Queue();
        RODAK_CHECK_EQ(f.Result(), "rejected");
        RODAK_CHECK_EQ(std::string(f.Last().reason), reason);
        RODAK_CHECK_FALSE(f.diagnostic.Pump());
    }
    RODAK_CHECK_EQ(f.deinit_calls.load(), 0u);
    RODAK_CHECK_EQ(f.restart_calls.load(), 0u);
    RODAK_CHECK_EQ(f.state_calls.load(), 0u);
    RODAK_CHECK_EQ(f.snapshot_calls.load(), 0u);
    f.busy = nullptr; f.Queue(); f.diagnostic.Pump();
    RODAK_CHECK_EQ(f.Result(), "three_task_pass");
}

RODAK_TEST("execution rechecks busy and accepted id remains consumed without mutating voice") {
    Fixture f;
    f.Queue(); f.busy = "ota_busy";
    RODAK_CHECK(f.diagnostic.Pump());
    RODAK_CHECK_EQ(std::string(f.Last().phase), "complete");
    RODAK_CHECK_EQ(f.Last().id, 1u);
    RODAK_CHECK_EQ(f.Result(), "rejected");
    RODAK_CHECK_EQ(f.deinit_calls.load(), 0u);
    RODAK_CHECK_EQ(f.restart_calls.load(), 0u);
    RODAK_CHECK_EQ(f.state_calls.load(), 0u);
    f.busy = nullptr; f.Queue();
    RODAK_CHECK_EQ(std::string(f.Last().reason), "stale_id");
}

RODAK_TEST("accepted printing reserves slot and result cannot precede accepted") {
    Fixture f;
    bool checked = false;
    f.emit_hook = [&](const auto& event) {
        if (std::string(event.phase) != "accepted") return;
        RODAK_CHECK(f.diagnostic.IsBusy());
        RODAK_CHECK_FALSE(f.diagnostic.Pump());
        f.Queue("2");
        RODAK_CHECK_EQ(std::string(f.Last().reason), "slot_busy");
        checked = true;
    };
    f.Queue();
    RODAK_CHECK(checked);
    f.diagnostic.Pump();
    RODAK_CHECK_EQ(f.Last().id, 1u);
    RODAK_CHECK_EQ(f.Result(), "three_task_pass");
}

RODAK_TEST("pending and executing refuse duplicate queue and execution has a single claimer") {
    Fixture f;
    f.Queue(); f.Queue("2");
    RODAK_CHECK_EQ(std::string(f.Last().reason), "slot_busy");
    Gate gate;
    f.deinit_hook = [&] {
        RODAK_CHECK(f.diagnostic.IsBusy());
        RODAK_CHECK_FALSE(f.diagnostic.Pump());
        gate.Enter();
    };
    auto worker = std::async(std::launch::async, [&] { return f.diagnostic.Pump(); });
    const bool reached = gate.Wait();
    f.Queue("3");
    const bool blocked = std::string(f.Last().reason) == "slot_busy";
    gate.Release();
    RODAK_CHECK(worker.get());
    RODAK_CHECK(reached && blocked);
    RODAK_CHECK_EQ(f.deinit_calls.load(), 1u);
    RODAK_CHECK_EQ(f.restart_calls.load(), 1u);
    RODAK_CHECK_EQ(f.Last().id, 1u);
}

RODAK_TEST("blocked admission output leaves accepting busy and main cannot claim before serial finishes") {
    Fixture f;
    Gate output;
    f.emit_hook = [&](const auto& event) {
        if (std::string(event.phase) == "accepted") output.Enter();
    };
    auto serial = std::async(std::launch::async, [&] { f.Queue("7"); });
    const bool printing = output.Wait();
    const bool busy = f.diagnostic.IsBusy();
    const bool claimed = f.diagnostic.Pump();
    f.Queue("8");
    const bool refused = std::string(f.Last().reason) == "slot_busy";
    output.Release(); serial.get();
    RODAK_CHECK(printing && busy && !claimed && refused);
    RODAK_CHECK(f.diagnostic.Pump());
    RODAK_CHECK_EQ(f.Last().id, 7u);
}

RODAK_TEST("resource checks can reenter busy and pump without holding the slot mutex") {
    Fixture f;
    unsigned checks = 0;
    f.busy_hook = [&] {
        ++checks;
        RODAK_CHECK(f.diagnostic.IsBusy());
        RODAK_CHECK_FALSE(f.diagnostic.Pump());
    };
    f.Queue(); f.diagnostic.Pump();
    RODAK_CHECK_EQ(checks, 2u);
    RODAK_CHECK_EQ(f.Result(), "three_task_pass");
}

RODAK_TEST("parallel serial requests produce one admitted id and the matching completed id") {
    Fixture f;
    std::thread first([&] { f.Queue("1"); });
    std::thread second([&] { f.Queue("2"); });
    first.join(); second.join();
    uint32_t accepted = 0;
    unsigned admissions = 0;
    for (const auto& event : f.events) if (std::string(event.phase) == "accepted") {
        accepted = event.id; ++admissions;
    }
    f.diagnostic.Pump();
    RODAK_CHECK_EQ(admissions, 1u);
    RODAK_CHECK_EQ(f.Last().id, accepted);
    RODAK_CHECK_EQ(f.deinit_calls.load(), 1u);
}

RODAK_TEST("voice mutations and other serial writes are denied throughout operation but AEC reads remain") {
    Fixture f;
    const std::vector<std::string> writes = {"RODAK_VOICE_TEST_V1 wake", "RODAK_VOICE_TEST_V1 stop",
        "RODAK_VOICE_TEST_V1 audio_clear", "RODAK_VOICE_TEST_V1 audio_live", "RODAK_VOICE_TEST_V1 audio_begin 1",
        "RODAK_VOICE_TEST_V1 audio_chunk 0 ff", "RODAK_VOICE_TEST_V1 aec_arm 10", "RODAK_VOICE_TEST_V1 aec_stop",
        "RODAK_VOICE_TEST_V1 aec_clear", "RODAK_RELEASE_TEST_V1 fail_alloc voice_task",
        "RODAK_APP_LAUNCH_V1 assistant", "RODAK_PROVISION_V1 {}"};
    const auto check = [&] {
        for (const auto& line : writes) RODAK_CHECK(f.diagnostic.BlocksSerialMutation(line));
        RODAK_CHECK_FALSE(f.diagnostic.BlocksSerialMutation("RODAK_VOICE_TEST_V1 aec_status"));
        RODAK_CHECK_FALSE(f.diagnostic.BlocksSerialMutation("RODAK_VOICE_TEST_V1 aec_read 0 0 1"));
    };
    f.Queue(); check();
    f.deinit_hook = check;
    f.diagnostic.Pump();
    for (const auto& line : writes) RODAK_CHECK_FALSE(f.diagnostic.BlocksSerialMutation(line));
}

RODAK_TEST("Listening three task cycle has ordered snapshots with no initializing observation after Deinit") {
    Fixture f;
    f.emit_hook = [&](const auto& event) {
        if (std::string(event.phase) == "after") {
            RODAK_CHECK_EQ(f.state_calls.load(), 1u);
            RODAK_CHECK_EQ(f.restart_calls.load(), 0u);
            RODAK_CHECK(event.stopped);
        }
    };
    f.Queue("73"); f.diagnostic.Pump();
    RODAK_CHECK_EQ(f.events.size(), 5u);
    const std::vector<std::string> phases = {"accepted", "before", "after", "recovered", "complete"};
    for (size_t i = 0; i < phases.size(); ++i) {
        RODAK_CHECK_EQ(std::string(f.events[i].phase), phases[i]);
        RODAK_CHECK_EQ(f.events[i].id, 73u);
    }
    RODAK_CHECK_EQ(f.Result(), "three_task_pass");
    RODAK_CHECK_FALSE(f.Last().snapshot.tasks.assistant);
    RODAK_CHECK(f.Last().snapshot.tasks.capture && f.Last().snapshot.tasks.supervisor);
    RODAK_CHECK_EQ(f.state_calls.load(), 2u);
    RODAK_CHECK_FALSE(f.diagnostic.Pump());
}

RODAK_TEST("idle and disabled idle are separate results with preserved enabled preference") {
    Fixture f;
    f.before.tasks.assistant = false; f.before.assistant_phase = Phase::kIdle;
    f.Queue(); f.diagnostic.Pump();
    RODAK_CHECK_EQ(f.Result(), "idle_cycle_pass");
    Fixture disabled;
    disabled.before_wake = disabled.after_wake = {false, false};
    disabled.before.tasks = {false, false, true}; disabled.before.assistant_phase = Phase::kIdle;
    disabled.restored.tasks = {false, false, true};
    disabled.Queue(); disabled.diagnostic.Pump();
    RODAK_CHECK_EQ(disabled.Result(), "disabled_idle_cycle_pass");
    RODAK_CHECK_FALSE(disabled.Last().enabled_before || disabled.Last().enabled_after);
}

RODAK_TEST("before tasks in non Listening phases or incomplete idle cannot pass the three task gate") {
    for (const auto phase : {Phase::kUnknown, Phase::kIdle, Phase::kConnecting, Phase::kSpeaking, Phase::kError}) {
        Fixture f; f.before.assistant_phase = phase;
        f.Queue(); f.diagnostic.Pump();
        RODAK_CHECK_EQ(f.Result(), "failed");
        RODAK_CHECK_EQ(std::string(f.Last().reason), "incomplete_before");
    }
    Fixture stopping; stopping.before.assistant_stopping = true;
    stopping.Queue(); stopping.diagnostic.Pump(); RODAK_CHECK_EQ(stopping.Result(), "failed");
    Fixture missing; missing.before.tasks = {false, false, true}; missing.before.assistant_phase = Phase::kIdle;
    missing.Queue(); missing.diagnostic.Pump(); RODAK_CHECK_EQ(missing.Result(), "failed");
    Fixture disabled; disabled.before_wake.enabled = disabled.after_wake.enabled = false;
    disabled.after_wake.listening = false; disabled.restored.tasks.capture = false;
    disabled.Queue(); disabled.diagnostic.Pump(); RODAK_CHECK_EQ(disabled.Result(), "failed");
}

RODAK_TEST("any remaining stopped task fails but still performs exactly one restoration") {
    for (unsigned index = 0; index < 3; ++index) {
        Fixture f;
        f.after.tasks = {index == 0, index == 1, index == 2};
        f.Queue(); f.diagnostic.Pump();
        RODAK_CHECK_EQ(f.Result(), "failed");
        RODAK_CHECK_EQ(std::string(f.Last().reason), "tasks_remain");
        RODAK_CHECK_EQ(f.restart_calls.load(), 1u);
        RODAK_CHECK(f.Last().recovered);
    }
}

RODAK_TEST("Start return alone cannot pass recovery and diagnostic never retries restoration") {
    for (unsigned failure = 0; failure < 6; ++failure) {
        Fixture f;
        if (failure == 0) f.restart_result = false;
        if (failure == 1) f.after_wake.enabled = false;
        if (failure == 2) f.after_wake.listening = false;
        if (failure == 3) f.restored.tasks.capture = false;
        if (failure == 4) f.restored.tasks.supervisor = false;
        if (failure == 5) f.restored.tasks.assistant = true;
        f.Queue(); f.diagnostic.Pump();
        RODAK_CHECK_EQ(f.Result(), "failed");
        RODAK_CHECK_EQ(std::string(f.Last().reason), "restore_failed");
        RODAK_CHECK_EQ(f.restart_calls.load(), 1u);
        RODAK_CHECK_FALSE(f.diagnostic.Pump());
    }
}

int main(int argc, char**) {
    if (argc > 1) {
        Fixture f; f.print = true; f.Queue("4294967295"); f.diagnostic.Pump();
        return f.Result() == "three_task_pass" ? 0 : 1;
    }
    return rodakos_test::RunAllTests();
}
