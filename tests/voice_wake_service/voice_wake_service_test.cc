#include "test_framework.h"
#include "phone_os/voice_wake_service.h"
#include "phone_os/voice_wake_settings.h"
#include "phone_os/time_service.h"
#include "settings.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <thread>

namespace wake_host {
void JoinTasks();
extern std::atomic<int64_t> unix_us;
extern std::atomic<unsigned> wall_reads;
}
namespace {
using namespace rodakos;
constexpr int64_t kUnix = 1800000000000;
class Runtime : public VoiceWakeRuntime {
public:
    bool Init() override { ++init_calls; return available; }
    void Deinit() override { listening = false; ++deinit_calls; }
    bool StartListening(std::function<void(const std::string&)> callback) override {
        ++start_calls;
        bool success = true;
        { std::lock_guard<std::mutex> lock(mutex);
          if (!starts.empty()) { success = starts.front(); starts.pop_front(); }
          if (success) { last_callback = std::move(callback); started_revisions.push_back(identity.revision); } }
        listening = success;
        return success;
    }
    void StopListening() override { ++stop_calls; listening = false; }
    bool IsListening() const override { return listening; }
    bool IsAvailable() const override { return available; }
    bool ConfigureWakeWord(const VoiceIdentityConfig& config) override {
        ++configure_calls;
        std::function<void(const VoiceIdentityConfig&)> hook;
        bool success = true;
        { std::lock_guard<std::mutex> lock(mutex);
          if (!configs.empty()) { success = configs.front(); configs.pop_front(); }
          hook = configure_hook; }
        if (hook) hook(config);
        if (success) { std::lock_guard<std::mutex> lock(mutex); identity = config; }
        return success;
    }
    const char* name() const override { return "host-runtime"; }
    const char* last_error() const override { ++legacy_error_reads; return "legacy runtime error"; }
    std::string LastErrorSnapshot() const override {
        std::lock_guard<std::mutex> lock(mutex);
        ++snapshot_error_reads;
        return error_text;
    }
    void SetError(std::string error) {
        std::lock_guard<std::mutex> lock(mutex); error_text = std::move(error);
    }
    void FailConfigurations(std::initializer_list<bool> results) {
        std::lock_guard<std::mutex> lock(mutex); configs = results;
    }
    void StartResults(std::initializer_list<bool> results) {
        std::lock_guard<std::mutex> lock(mutex); starts = results;
    }
    mutable std::mutex mutex;
    std::string error_text = "injected runtime failure";
    mutable std::atomic<unsigned> snapshot_error_reads{0}, legacy_error_reads{0};
    VoiceIdentityConfig identity = DefaultVoiceIdentityConfig();
    std::deque<bool> configs;
    std::deque<bool> starts;
    std::vector<uint32_t> started_revisions;
    std::function<void(const VoiceIdentityConfig&)> configure_hook;
    std::function<void(const std::string&)> last_callback;
    std::atomic<bool> available{true};
    std::atomic<bool> listening{false};
    std::atomic<unsigned> init_calls{0}, configure_calls{0}, start_calls{0}, stop_calls{0}, deinit_calls{0};
};

VoiceIdentityConfig Config(uint32_t revision, bool temporary = false) {
    auto config = DefaultVoiceIdentityConfig();
    config.name = "identity-" + std::to_string(revision);
    config.revision = revision;
    config.mode = temporary ? VoiceIdentityApplyMode::kTemporary : VoiceIdentityApplyMode::kPersistent;
    config.expires_at_ms = temporary ? kUnix + 5000 : 0;
    return config;
}
void StoreRecord(const VoiceIdentityRecord& record, bool enabled) {
    std::string json, error;
    RODAK_CHECK(EncodeVoiceIdentityRecord(record, json, error));
    std::lock_guard<std::mutex> lock(wake_host::StoreMutex());
    wake_host::Store().strings["voice_wake:identity"] = json;
    wake_host::Store().booleans["voice_wake:enabled"] = enabled;
}
VoiceIdentityRecord Stored() {
    VoiceIdentityRecord record;
    std::string error;
    RODAK_CHECK(LoadVoiceWakeIdentitySettings(record, error));
    return record;
}
struct Fixture {
    Runtime runtime;
    VoiceAssistantService assistant;
    std::mutex clock_mutex;
    VoiceIdentityClockSnapshot clock{true, kUnix, 1000};
    std::unique_ptr<VoiceWakeService> service;
    Fixture(bool enabled = true, VoiceIdentityRecord record = {}) {
        wake_host::ResetStore();
        StoreRecord(record, enabled);
        service = std::make_unique<VoiceWakeService>(assistant, runtime, [&]() {
            std::lock_guard<std::mutex> lock(clock_mutex); return clock;
        });
    }
    ~Fixture() { service.reset(); wake_host::JoinTasks(); }
    void Start() { RODAK_CHECK(service->Start()); }
    void Time(bool valid, int64_t unix_ms, int64_t monotonic_ms) {
        std::lock_guard<std::mutex> lock(clock_mutex); clock = {valid, unix_ms, monotonic_ms};
    }
    bool Apply(VoiceIdentityConfig config) { std::string error; return service->ApplyVoiceIdentity(config, error); }
};
}

RODAK_TEST("identity Init and GetState do not load a disabled wake runtime") {
    Fixture f(false);
    RODAK_CHECK(f.service->Init());
    f.Start();
    const auto pending = f.service->GetState();
    RODAK_CHECK_EQ(f.runtime.init_calls.load(), 0u);
    RODAK_CHECK_EQ(f.runtime.start_calls.load(), 0u);
    RODAK_CHECK_EQ(pending.voice_identity_status, "pending");
    RODAK_CHECK_FALSE(pending.voice_identity_active_confirmed);
    RODAK_CHECK(f.Apply(DefaultVoiceIdentityConfig()));
    RODAK_CHECK(f.service->GetState().voice_identity_active_confirmed);
    RODAK_CHECK_EQ(f.runtime.start_calls.load(), 0u);
}

RODAK_TEST("identity applies complete persistent record and idempotence does not reconfigure") {
    Fixture f;
    f.Start();
    auto next = Config(3);
    RODAK_CHECK(f.Apply(next));
    const auto before = f.runtime.configure_calls.load();
    RODAK_CHECK(f.Apply(next));
    RODAK_CHECK_EQ(before, f.runtime.configure_calls.load());
    RODAK_CHECK_FALSE(f.Apply(Config(2)));
    next.name = "conflict";
    RODAK_CHECK_FALSE(f.Apply(next));
    const auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity.revision, 3u);
    RODAK_CHECK_EQ(state.voice_identity_revision_watermark, 3u);
    RODAK_CHECK(state.voice_identity_active_confirmed);
    const auto record = Stored();
    RODAK_CHECK(VoiceIdentityConfigEquals(record.persistent, record.active));
    RODAK_CHECK(VoiceIdentityConfigEquals(record.active, record.last_accepted));
}

RODAK_TEST("temporary expiry uses Unix and keeps accepted revision through wall rollback and replay") {
    Fixture f;
    f.Start();
    auto temporary = Config(9, true);
    RODAK_CHECK(f.Apply(temporary));
    f.Time(true, kUnix - 100000, 6000);
    auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity_status, "expired");
    RODAK_CHECK_EQ(state.voice_identity.revision, 1u);
    RODAK_CHECK_EQ(state.voice_identity_revision_watermark, 9u);
    RODAK_CHECK(f.Apply(temporary));
    RODAK_CHECK_EQ(f.service->GetState().voice_identity.revision, 1u);
    RODAK_CHECK_FALSE(f.Apply(Config(8)));
    RODAK_CHECK_EQ(Stored().last_accepted.revision, 9u);
}

RODAK_TEST("untrusted boot clock uses persistent fallback and later configures a valid temporary identity") {
    VoiceIdentityRecord record;
    record.active = record.last_accepted = Config(4, true);
    Fixture f(true, record);
    f.Time(false, 0, 0);
    f.Start();
    auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity_status, "pending_clock");
    RODAK_CHECK_EQ(state.voice_identity.revision, 1u);
    RODAK_CHECK_FALSE(state.voice_identity_active_confirmed);
    RODAK_CHECK_EQ(Stored().active.revision, 4u);
    f.Time(true, kUnix + 1000, 1000);
    state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity.revision, 4u);
    RODAK_CHECK(state.voice_identity_active_confirmed);
}

RODAK_TEST("disabled unconfigured expiry persists fallback without claiming runtime expiry") {
    VoiceIdentityRecord record;
    record.active = record.last_accepted = Config(4, true);
    Fixture f(false, record);
    f.Time(true, kUnix + 6000, 10);
    f.Start();
    const auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity_status, "pending");
    RODAK_CHECK_FALSE(state.voice_identity_active_confirmed);
    RODAK_CHECK_EQ(f.runtime.init_calls.load(), 0u);
    RODAK_CHECK_EQ(Stored().active.revision, 1u);
    RODAK_CHECK_EQ(Stored().last_accepted.revision, 4u);
}

RODAK_TEST("temporary input requires synchronized Unix time and rejects already expired input") {
    Fixture f;
    f.Start();
    f.Time(false, 0, 1000);
    RODAK_CHECK_FALSE(f.Apply(Config(2, true)));
    f.Time(true, kUnix + 5000, 1000);
    RODAK_CHECK_FALSE(f.Apply(Config(2, true)));
    RODAK_CHECK_EQ(f.service->GetState().voice_identity_revision_watermark, 1u);
}

RODAK_TEST("known unchanged save restores runtime and listening before reporting rejection") {
    Fixture f;
    f.Start();
    wake_host::Fault({SettingsStringWriteStatus::kError, false, true});
    RODAK_CHECK_FALSE(f.Apply(Config(2)));
    const auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity.revision, 1u);
    RODAK_CHECK_EQ(state.voice_identity_status, "rejected");
    RODAK_CHECK(state.voice_identity_active_confirmed && state.listening);
    RODAK_CHECK_EQ(Stored().active.revision, 1u);
}

RODAK_TEST("runtime rejection does not persist candidate and verifies the old graph restoration") {
    Fixture f;
    f.Start();
    f.runtime.FailConfigurations({false, true});
    RODAK_CHECK_FALSE(f.Apply(Config(2)));
    auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity_status, "rejected");
    RODAK_CHECK(state.voice_identity_active_confirmed && state.listening);
    RODAK_CHECK_EQ(Stored().last_accepted.revision, 1u);
}

RODAK_TEST("runtime restoration failure freezes ordinary requests and remains unconfirmed") {
    Fixture f;
    f.Start();
    f.runtime.FailConfigurations({false, false});
    RODAK_CHECK_FALSE(f.Apply(Config(2)));
    RODAK_CHECK_EQ(f.service->GetState().voice_identity_status, "recovery_failed");
    RODAK_CHECK_FALSE(f.service->GetState().voice_identity_active_confirmed);
    const auto attempts = f.runtime.configure_calls.load();
    RODAK_CHECK_FALSE(f.Apply(Config(3)));
    f.service->RejectVoiceIdentity("another parse failure");
    RODAK_CHECK_EQ(f.service->GetState().voice_identity_status, "recovery_failed");
    RODAK_CHECK_EQ(attempts, f.runtime.configure_calls.load());
    f.service->Deinit();
    wake_host::JoinTasks();
    f.Start();
    RODAK_CHECK(f.service->GetState().voice_identity_active_confirmed);
}

RODAK_TEST("indeterminate save keeps storage uncertainty and cannot recover through GetState") {
    Fixture f;
    f.Start();
    wake_host::Fault({SettingsStringWriteStatus::kRemoveFailed, true, false});
    RODAK_CHECK_FALSE(f.Apply(Config(2)));
    for (int i = 0; i < 3; ++i) {
        const auto state = f.service->GetState();
        RODAK_CHECK_EQ(state.voice_identity_status, "recovery_failed");
        RODAK_CHECK_FALSE(state.voice_identity_active_confirmed || state.listening);
    }
    RODAK_CHECK_EQ(Stored().active.revision, 2u);
    RODAK_CHECK_FALSE(f.Apply(Config(3)));
}

RODAK_TEST("failed migration remains latched even when its errored write left a complete record") {
    Fixture f(false);
    { std::lock_guard<std::mutex> lock(wake_host::StoreMutex()); wake_host::Store().strings.clear(); }
    wake_host::Fault({SettingsStringWriteStatus::kRemoveFailed, true, false});
    RODAK_CHECK_FALSE(f.service->Init());
    RODAK_CHECK_EQ(f.service->GetState().voice_identity_status, "recovery_failed");
    RODAK_CHECK_FALSE(f.service->GetState().voice_identity_active_confirmed);
    RODAK_CHECK_FALSE(f.service->Init());
    RODAK_CHECK_FALSE(f.Apply(Config(2)));
}

RODAK_TEST("listener restart failure restores both runtime and durable record") {
    Fixture f;
    f.Start();
    f.runtime.StartResults({false, true});
    RODAK_CHECK_FALSE(f.Apply(Config(2)));
    const auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity.revision, 1u);
    RODAK_CHECK(state.voice_identity_active_confirmed && state.listening);
    RODAK_CHECK_EQ(Stored().active.revision, 1u);
}

RODAK_TEST("failed persistent compensation never reports rejected with a confirmed old runtime") {
    Fixture f;
    f.Start();
    f.runtime.StartResults({false});
    wake_host::Fault({});
    wake_host::Fault({SettingsStringWriteStatus::kError, false, true});
    RODAK_CHECK_FALSE(f.Apply(Config(2)));
    const auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity_status, "recovery_failed");
    RODAK_CHECK_FALSE(state.voice_identity_active_confirmed || state.listening);
}

RODAK_TEST("expiry runtime failure does not claim expired or continue expired listening") {
    Fixture f;
    f.Start();
    RODAK_CHECK(f.Apply(Config(2, true)));
    f.runtime.FailConfigurations({false, true});
    f.Time(true, kUnix + 6000, 7000);
    const auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity_status, "recovery_failed");
    RODAK_CHECK_FALSE(state.voice_identity_active_confirmed || state.listening);
}

RODAK_TEST("concurrent revisions and GetState are serialized around the full transaction") {
    Fixture f;
    f.Start();
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    { std::lock_guard<std::mutex> lock(f.runtime.mutex);
      f.runtime.configure_hook = [&](const VoiceIdentityConfig& config) {
          if (config.revision == 3) { entered.set_value(); gate.wait(); }
      }; }
    auto high = std::async(std::launch::async, [&]() { return f.Apply(Config(3)); });
    entered.get_future().wait();
    auto lower = std::async(std::launch::async, [&]() { return f.Apply(Config(2)); });
    auto snapshot = std::async(std::launch::async, [&]() { return f.service->GetState(); });
    const bool blocked = snapshot.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout;
    release.set_value();
    RODAK_CHECK(blocked);
    RODAK_CHECK(high.get());
    RODAK_CHECK_FALSE(lower.get());
    const auto state = snapshot.get();
    RODAK_CHECK_EQ(state.voice_identity.revision, 3u);
    RODAK_CHECK_EQ(state.voice_identity_revision_watermark, 3u);
    RODAK_CHECK(state.voice_identity_active_confirmed);
}

RODAK_TEST("Stop cancels old wake callbacks including speaking interruption") {
    Fixture f;
    f.Start();
    std::function<void(const std::string&)> stale;
    { std::lock_guard<std::mutex> lock(f.runtime.mutex); stale = f.runtime.last_callback; }
    f.service->Stop();
    { std::lock_guard<std::mutex> lock(f.assistant.mutex); f.assistant.state.phase = VoiceAssistantPhase::kSpeaking; }
    stale("old wake");
    RODAK_CHECK_EQ(f.assistant.interrupts, 0u);
    RODAK_CHECK_FALSE(f.service->GetState().listening);
}

RODAK_TEST("identity and disable generations reject stale barge in but current callback can interrupt") {
    Fixture f;
    f.Start();
    std::function<void(const std::string&)> old, current;
    { std::lock_guard<std::mutex> lock(f.runtime.mutex); old = f.runtime.last_callback; }
    RODAK_CHECK(f.Apply(Config(2)));
    { std::lock_guard<std::mutex> lock(f.runtime.mutex); current = f.runtime.last_callback; }
    { std::lock_guard<std::mutex> lock(f.assistant.mutex); f.assistant.state.phase = VoiceAssistantPhase::kSpeaking; }
    old("old identity");
    RODAK_CHECK_EQ(f.assistant.interrupts, 0u);
    current("current identity");
    RODAK_CHECK_EQ(f.assistant.interrupts, 1u);
    RODAK_CHECK(f.service->SetEnabled(false));
    { std::lock_guard<std::mutex> lock(f.assistant.mutex); f.assistant.state.phase = VoiceAssistantPhase::kSpeaking; }
    current("disabled");
    RODAK_CHECK_EQ(f.assistant.interrupts, 1u);
}

RODAK_TEST("wake callback invalidated while reading assistant state cannot interrupt later TTS") {
    for (unsigned change = 0; change < 3; ++change) {
        Fixture f;
        f.Start();
        std::function<void(const std::string&)> callback;
        { std::lock_guard<std::mutex> lock(f.runtime.mutex); callback = f.runtime.last_callback; }
        { std::lock_guard<std::mutex> lock(f.assistant.mutex);
          f.assistant.state.phase = VoiceAssistantPhase::kSpeaking; }
        std::promise<void> entered, release;
        auto gate = release.get_future().share();
        auto wake = std::async(std::launch::async, [&]() {
            { std::lock_guard<std::mutex> lock(f.assistant.mutex);
              f.assistant.get_state_hook = [&, caller = std::this_thread::get_id()]() {
                  if (std::this_thread::get_id() != caller) return;
                  entered.set_value();
                  gate.wait();
              }; }
            callback("old wake");
        });
        entered.get_future().wait();
        bool changed = true;
        if (change == 0) changed = f.Apply(Config(2));
        else if (change == 1) changed = f.service->SetEnabled(false);
        else f.service->Stop();
        { std::lock_guard<std::mutex> lock(f.assistant.mutex);
          f.assistant.state.phase = VoiceAssistantPhase::kSpeaking;
          f.assistant.get_state_hook = {}; }
        release.set_value();
        wake.get();
        RODAK_CHECK(changed);
        RODAK_CHECK_EQ(f.assistant.interrupts, 0u);
    }
}

RODAK_TEST("Unix adapter validates and returns one gettimeofday snapshot") {
    wake_host::unix_us = 1800000000123456;
    wake_host::wall_reads = 0;
    int64_t value = 0;
    RODAK_CHECK(TimeServiceUnixTimeMs(value));
    RODAK_CHECK_EQ(value, 1800000000123);
    RODAK_CHECK_EQ(wake_host::wall_reads.load(), 1u);
    wake_host::unix_us = 1;
    RODAK_CHECK_FALSE(TimeServiceUnixTimeMs(value));
    RODAK_CHECK_EQ(value, 0);
}

RODAK_TEST("forward clock step expires temporary identity without waiting for monotonic deadline") {
    Fixture f;
    f.Start();
    RODAK_CHECK(f.Apply(Config(2, true)));
    f.Time(true, kUnix + 9000, 1001);
    const auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity_status, "expired");
    RODAK_CHECK(state.voice_identity_active_confirmed);
    RODAK_CHECK_EQ(state.voice_identity_revision_watermark, 2u);
}

RODAK_TEST("clock loss uses fallback without renewing the original monotonic lease") {
    Fixture f;
    f.Start();
    RODAK_CHECK(f.Apply(Config(2, true)));
    f.Time(false, 0, 2000);
    RODAK_CHECK_EQ(f.service->GetState().voice_identity_status, "pending_clock");
    f.Time(true, kUnix + 1000, 3000);
    RODAK_CHECK_EQ(f.service->GetState().voice_identity.revision, 2u);
    f.Time(true, kUnix + 1000, 6000);
    RODAK_CHECK_EQ(f.service->GetState().voice_identity_status, "expired");
}

RODAK_TEST("temporary reboot waits for trusted time then expires without lowering the watermark") {
    Fixture f;
    f.Start();
    RODAK_CHECK(f.Apply(Config(5, true)));
    f.service->Deinit();
    wake_host::JoinTasks();
    f.Time(false, 0, 0);
    f.Start();
    RODAK_CHECK_EQ(f.service->GetState().voice_identity_status, "pending_clock");
    f.Time(true, kUnix + 6000, 2000);
    const auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity_status, "expired");
    RODAK_CHECK_EQ(state.voice_identity_revision_watermark, 5u);
}

RODAK_TEST("identity expiring during configuration never starts the expired temporary listener") {
    Fixture f;
    f.Start();
    { std::lock_guard<std::mutex> lock(f.runtime.mutex);
      f.runtime.configure_hook = [&](const VoiceIdentityConfig& config) {
          if (config.revision == 2) f.Time(true, kUnix + 6000, 7000);
      }; }
    RODAK_CHECK(f.Apply(Config(2, true)));
    const auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity_status, "expired");
    RODAK_CHECK_EQ(state.voice_identity_revision_watermark, 2u);
    std::lock_guard<std::mutex> lock(f.runtime.mutex);
    for (const auto revision : f.runtime.started_revisions) RODAK_CHECK_EQ(revision, 1u);
}

RODAK_TEST("failed candidate cannot restart previous temporary identity that expired during configuration") {
    Fixture f;
    f.Start();
    RODAK_CHECK(f.Apply(Config(2, true)));
    const auto starts = f.runtime.start_calls.load();
    f.runtime.FailConfigurations({false, true});
    { std::lock_guard<std::mutex> lock(f.runtime.mutex);
      f.runtime.configure_hook = [&](const VoiceIdentityConfig& config) {
          if (config.revision == 3) f.Time(true, kUnix + 6000, 7000);
      }; }
    RODAK_CHECK_FALSE(f.Apply(Config(3)));
    const auto state = f.service->GetState();
    RODAK_CHECK_EQ(state.voice_identity_status, "recovery_failed");
    RODAK_CHECK_FALSE(state.voice_identity_active_confirmed);
    RODAK_CHECK_FALSE(state.listening);
    RODAK_CHECK_EQ(f.runtime.start_calls.load(), starts);
    RODAK_CHECK_EQ(state.voice_identity_revision_watermark, 2u);
}

RODAK_TEST("service reads owned runtime error snapshots while errors change concurrently") {
    Fixture f;
    f.Start();
    const std::string first(256, 'a'), second(1024, 'b');
    f.runtime.SetError(first);
    std::atomic<bool> writing{true};
    std::promise<void> entered;
    auto writer = std::async(std::launch::async, [&]() {
        entered.set_value();
        while (writing) {
            f.runtime.SetError(first);
            f.runtime.SetError(second);
            std::this_thread::yield();
        }
    });
    entered.get_future().wait();
    bool rejected = true, valid_snapshots = true;
    for (unsigned attempt = 0; attempt < 25; ++attempt) {
        f.runtime.FailConfigurations({false, true});
        rejected = !f.Apply(Config(2)) && rejected;
        const auto error = f.service->GetState().voice_identity_error;
        valid_snapshots = (error == first || error == second) && valid_snapshots;
    }
    writing = false;
    writer.get();
    RODAK_CHECK(rejected);
    RODAK_CHECK(valid_snapshots);
    RODAK_CHECK(f.runtime.snapshot_error_reads.load() >= 25u);
    RODAK_CHECK_EQ(f.runtime.legacy_error_reads.load(), 0u);
}
