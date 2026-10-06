#include "test_framework.h"
#include "host_files.h"
#include "host_runtime.h"
#include "stdio_faults.h"
#include "phone_os/recording_service.h"
#include "phone_os/audio_focus_service.h"
#include "rodakos_adapters/audio_codec_input.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <thread>
#include <unistd.h>

namespace {
using namespace rodakos;
using recording_host::StdioFaults;
bool Active(RecordingStatus status) {
    return status == RecordingStatus::kStarting || status == RecordingStatus::kRecording || status == RecordingStatus::kStopping;
}
template <class T> bool WaitUntil(T condition, unsigned milliseconds = 2000) {
    for (unsigned i = 0; i < milliseconds; ++i) {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
std::string NewRoot() {
    static std::atomic<unsigned> serial{0};
    return (std::filesystem::temp_directory_path() /
        ("rodakos-recorder-" + std::to_string(getpid()) + "-" + std::to_string(++serial))).string();
}
std::vector<uint8_t> Read(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
uint32_t Le32(const std::vector<uint8_t>& bytes, size_t index) {
    return static_cast<uint32_t>(bytes.at(index)) | (static_cast<uint32_t>(bytes.at(index + 1)) << 8) |
        (static_cast<uint32_t>(bytes.at(index + 2)) << 16) | (static_cast<uint32_t>(bytes.at(index + 3)) << 24);
}
struct Fixture {
    std::string root = NewRoot();
    recording_host::Files files{root};
    AudioCodecInput input;
    AudioFocusService focus;
    std::unique_ptr<RecordingService> recording = std::make_unique<RecordingService>(input, &files, &focus);
    Fixture() {
        recording_host::ResetStdioFaults(); audio_host::fail_task_creation = false;
        audio_host::task_start_hook = {}; fake_heap_allocations_until_failure = -1;
    }
    ~Fixture() {
        recording.reset(); audio_host::JoinTasks(); recording_host::ResetStdioFaults();
        audio_host::task_start_hook = {}; fake_heap_allocations_until_failure = -1;
        std::filesystem::remove_all(root);
    }
    RecordingState Wait() {
        RODAK_CHECK(WaitUntil([&]() { return !Active(recording->GetState().status); }));
        audio_host::JoinTasks(); return recording->GetState();
    }
    RecordingState Capture() {
        RODAK_CHECK(recording->Start());
        RODAK_CHECK(WaitUntil([&]() { const auto state = recording->GetState(); return state.bytes_written >= 4096 || state.status == RecordingStatus::kError; }));
        recording->Stop(); return Wait();
    }
    size_t FileCount() const {
        const auto directory = std::filesystem::path(root) / "recordings";
        return std::filesystem::exists(directory) ? static_cast<size_t>(std::distance(
            std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator())) : 0;
    }
};
void CheckError(const RecordingState& state) {
    RODAK_CHECK_EQ(state.status, RecordingStatus::kError);
    RODAK_CHECK_FALSE(state.last_error.empty());
    RODAK_CHECK(state.message != "Saved");
}
}

RODAK_TEST("normal recording publishes Saved only with complete header payload flush and close") {
    Fixture f; const auto state = f.Capture();
    RODAK_CHECK_EQ(state.status, RecordingStatus::kCompleted);
    RODAK_CHECK_EQ(state.message, "Saved");
    const auto bytes = Read(state.full_path);
    RODAK_CHECK_EQ(bytes.size(), state.bytes_written + 44);
    RODAK_CHECK_EQ(Le32(bytes, 4), state.bytes_written + 36);
    RODAK_CHECK_EQ(Le32(bytes, 40), state.bytes_written);
    RODAK_CHECK_EQ(f.recording->GetRecordings().size(), 1u);
    RODAK_CHECK_EQ(f.focus.releases.load(), 1u);
    RODAK_CHECK_EQ(f.focus.invalid_releases.load(), 0u);
    RODAK_CHECK_EQ(f.focus.last_request.owner, "recorder");
    RODAK_CHECK_EQ(f.focus.last_request.gain, AudioFocusGain::kExclusive);
    RODAK_CHECK_FALSE(f.focus.last_request.resume_on_release);
    RODAK_CHECK(f.focus.last_request.release_playback_hardware);
    const auto events = recording_host::StdioEvents();
    const auto final = std::find(events.begin(), events.end(), "final-header");
    RODAK_CHECK(final != events.end());
    RODAK_CHECK(std::find(final, events.end(), "flush") < std::find(final, events.end(), "close"));
    RODAK_CHECK_EQ(recording_host::OutstandingRecordingDescriptors(), 0u);
}

RODAK_TEST("seek final header flush and close faults cannot publish Saved and allow retry") {
    for (unsigned step = 0; step < 4; ++step) {
        Fixture f; StdioFaults fault;
        fault.seek = step == 0; fault.final_header = step == 1; fault.flush = step == 2; fault.close = step == 3;
        recording_host::SetStdioFaults(fault);
        const auto error = f.Capture(); CheckError(error);
        RODAK_CHECK_EQ(f.FileCount(), 0u);
        RODAK_CHECK_EQ(f.focus.releases.load(), 1u);
        RODAK_CHECK_EQ(recording_host::OutstandingRecordingDescriptors(), 0u);
        recording_host::ResetStdioFaults();
        RODAK_CHECK_EQ(f.Capture().status, RecordingStatus::kCompleted);
    }
}

RODAK_TEST("initial header data and fdopen faults preserve specific errors and release ownership") {
    for (unsigned step = 0; step < 4; ++step) {
        Fixture f; StdioFaults fault;
        fault.initial_header = step == 0; fault.data_write = step == 1; fault.fdopen = step == 2; fault.create = step == 3;
        fault.flush = fault.close = true;
        recording_host::SetStdioFaults(fault);
        RODAK_CHECK(f.recording->Start()); const auto state = f.Wait(); CheckError(state);
        if (step == 0) RODAK_CHECK_EQ(state.message, "Cannot write WAV header");
        if (step == 1) RODAK_CHECK_EQ(state.message, "SD write failed");
        RODAK_CHECK_EQ(f.FileCount(), 0u);
        RODAK_CHECK_EQ(recording_host::OutstandingRecordingDescriptors(), 0u);
        RODAK_CHECK_EQ(f.focus.releases.load(), 1u);
        recording_host::ResetStdioFaults();
        RODAK_CHECK_EQ(f.Capture().status, RecordingStatus::kCompleted);
    }
}

RODAK_TEST("same-second recordings use exclusive names and never replace prior bytes") {
    Fixture f; const auto first = f.Capture(); const auto original = Read(first.full_path);
    const auto second = f.Capture();
    RODAK_CHECK(first.full_path != second.full_path);
    RODAK_CHECK_EQ(Read(first.full_path), original);
    RODAK_CHECK_EQ(f.FileCount(), 2u);
    StdioFaults fault; fault.all_names_exist = true; recording_host::SetStdioFaults(fault);
    RODAK_CHECK(f.recording->Start()); CheckError(f.Wait());
    RODAK_CHECK_EQ(Read(first.full_path), original);
    RODAK_CHECK_EQ(f.FileCount(), 2u);
}

RODAK_TEST("simultaneous service instances allocate distinct files under one directory") {
    Fixture f;
    AudioCodecInput other_input; AudioFocusService other_focus;
    RecordingService other(other_input, &f.files, &other_focus);
    std::promise<void> first_read, release;
    auto gate = release.get_future().share();
    std::atomic<unsigned> reads{0};
    auto read_hook = [&](void* bytes, int count) {
        if (reads.fetch_add(1) == 0) first_read.set_value();
        gate.wait();
        std::memset(bytes, 0, static_cast<size_t>(count));
        return true;
    };
    f.input.read_hook = read_hook;
    other_input.read_hook = read_hook;
    RODAK_CHECK(f.recording->Start()); RODAK_CHECK(other.Start());
    first_read.get_future().wait();
    RODAK_CHECK(WaitUntil([&]() { return reads.load() >= 2; }));
    release.set_value();
    RODAK_CHECK(WaitUntil([&]() { return f.recording->GetState().bytes_written >= 4096 && other.GetState().bytes_written >= 4096; }));
    f.recording->Stop(); other.Stop(); audio_host::JoinTasks();
    RODAK_CHECK_EQ(f.recording->GetState().status, RecordingStatus::kCompleted);
    RODAK_CHECK_EQ(other.GetState().status, RecordingStatus::kCompleted);
    RODAK_CHECK(f.recording->GetState().full_path != other.GetState().full_path);
    RODAK_CHECK_EQ(f.FileCount(), 2u);
}

RODAK_TEST("stop before task starts cancels without opening codec or creating a file") {
    Fixture f; std::promise<void> release;
    auto gate = release.get_future().share(); audio_host::task_start_hook = [gate]() { gate.wait(); };
    RODAK_CHECK(f.recording->Start());
    auto stop = std::async(std::launch::async, [&]() { f.recording->Stop(); });
    const bool stopping = WaitUntil([&]() { return f.recording->GetState().status == RecordingStatus::kStopping; });
    release.set_value(); stop.get(); const auto state = f.Wait();
    RODAK_CHECK(stopping); RODAK_CHECK_EQ(state.status, RecordingStatus::kIdle);
    RODAK_CHECK_EQ(state.message, "Cancelled");
    RODAK_CHECK_EQ(f.input.open_calls.load(), 0u); RODAK_CHECK_EQ(f.FileCount(), 0u);
    RODAK_CHECK_EQ(f.focus.releases.load(), 1u);
}

RODAK_TEST("stop during storage or focus startup cancels before worker admission") {
    for (bool at_focus : {false, true}) {
        Fixture f; std::promise<void> entered, release;
        auto gate = release.get_future().share();
        auto hook = [&]() { entered.set_value(); gate.wait(); };
        if (at_focus) f.focus.request_hook = hook; else f.files.init_hook = hook;
        auto start = std::async(std::launch::async, [&]() { return f.recording->Start(); });
        entered.get_future().wait();
        auto stop = std::async(std::launch::async, [&]() { f.recording->Stop(); });
        const bool stopping = WaitUntil([&]() { return f.recording->GetState().status == RecordingStatus::kStopping; });
        release.set_value(); const bool accepted = start.get(); stop.get();
        RODAK_CHECK(stopping); RODAK_CHECK_FALSE(accepted);
        RODAK_CHECK_EQ(f.recording->GetState().message, "Cancelled");
        RODAK_CHECK_EQ(f.FileCount(), 0u); RODAK_CHECK_EQ(f.input.open_calls.load(), 0u);
        RODAK_CHECK_EQ(f.focus.releases.load(), at_focus ? 1u : 0u);
    }
}

RODAK_TEST("codec focus task and allocation failures release resources and permit retry") {
    for (unsigned step = 0; step < 5; ++step) {
        Fixture f;
        f.focus.fail_request = step == 0; f.input.fail_open = step == 1; f.input.fail_read = step == 2;
        audio_host::fail_task_creation = step == 3;
        if (step == 4) fake_heap_allocations_until_failure = 0;
        const bool accepted = f.recording->Start();
        if (step == 0 || step == 3) RODAK_CHECK_FALSE(accepted); else RODAK_CHECK(accepted);
        CheckError(f.Wait());
        RODAK_CHECK_EQ(f.FileCount(), 0u);
        RODAK_CHECK_EQ(f.focus.releases.load(), step == 0 ? 0u : 1u);
        RODAK_CHECK_EQ(f.focus.invalid_releases.load(), 0u);
        f.focus.fail_request = f.input.fail_open = f.input.fail_read = false;
        audio_host::fail_task_creation = false; fake_heap_allocations_until_failure = -1;
        RODAK_CHECK_EQ(f.Capture().status, RecordingStatus::kCompleted);
    }
}

RODAK_TEST("library read or close failures clear partial lists without changing saved outcome") {
    Fixture f; RODAK_CHECK_EQ(f.Capture().status, RecordingStatus::kCompleted);
    RODAK_CHECK_EQ(f.Capture().status, RecordingStatus::kCompleted);
    for (bool close : {false, true}) {
        StdioFaults fault;
        if (close) fault.library_close_error = 2; else fault.library_read_error = 2;
        recording_host::SetStdioFaults(fault);
        RODAK_CHECK_FALSE(f.recording->RefreshRecordings());
        const auto state = f.recording->GetState();
        RODAK_CHECK_EQ(state.status, RecordingStatus::kCompleted);
        RODAK_CHECK_EQ(state.message, "Saved");
        RODAK_CHECK_FALSE(state.library_error.empty());
        RODAK_CHECK(f.recording->GetRecordings().empty());
        recording_host::ResetStdioFaults();
        RODAK_CHECK(f.recording->RefreshRecordings());
        RODAK_CHECK_EQ(f.recording->GetRecordings().size(), 2u);
    }
}

RODAK_TEST("cleanup failure retains original save error and never advertises incomplete WAV") {
    Fixture f; StdioFaults fault; fault.data_write = fault.remove = true;
    recording_host::SetStdioFaults(fault);
    RODAK_CHECK(f.recording->Start()); const auto state = f.Wait(); CheckError(state);
    RODAK_CHECK(state.message.find("SD write failed") == 0);
    RODAK_CHECK(state.message.find("could not be removed") != std::string::npos);
    RODAK_CHECK_EQ(f.FileCount(), 1u);
    recording_host::ResetStdioFaults();
    RODAK_CHECK(f.recording->RefreshRecordings());
    RODAK_CHECK(f.recording->GetRecordings().empty());
}

RODAK_TEST("final close completes before Saved while active controls cannot overwrite stopping state") {
    for (const bool at_header : {false, true}) {
        Fixture f; std::promise<void> entered, release; auto gate = release.get_future().share();
        StdioFaults fault;
        auto hook = [&]() { entered.set_value(); gate.wait(); };
        if (at_header) fault.final_header_hook = hook; else fault.close_hook = hook;
        recording_host::SetStdioFaults(fault);
        RODAK_CHECK(f.recording->Start());
        RODAK_CHECK(WaitUntil([&]() { return f.recording->GetState().bytes_written >= 4096; }));
        auto stop = std::async(std::launch::async, [&]() { f.recording->Stop(); });
        entered.get_future().wait();
        auto repeated_stop = std::async(std::launch::async, [&]() { f.recording->Stop(); });
        const auto pending = f.recording->GetState();
        const auto focus_releases = f.focus.releases.load();
        release.set_value(); stop.get(); repeated_stop.get(); const auto state = f.Wait();
        RODAK_CHECK_EQ(pending.status, RecordingStatus::kStopping);
        RODAK_CHECK_EQ(focus_releases, 0u);
        RODAK_CHECK_EQ(state.status, RecordingStatus::kCompleted);
        RODAK_CHECK_EQ(f.focus.releases.load(), 1u);
    }
}

RODAK_TEST("destructor waits for in-flight ADC read before releasing codec focus and state") {
    Fixture f; std::promise<void> entered, release; auto gate = release.get_future().share();
    f.input.read_hook = [&](void* bytes, int count) { entered.set_value(); gate.wait(); std::memset(bytes, 0, count); return true; };
    RODAK_CHECK(f.recording->Start()); entered.get_future().wait();
    auto destroy = std::async(std::launch::async, [&]() { f.recording.reset(); });
    const bool waited = destroy.wait_for(std::chrono::milliseconds(180)) == std::future_status::timeout;
    const auto releases = f.focus.releases.load();
    release.set_value(); destroy.get(); audio_host::JoinTasks();
    RODAK_CHECK(waited); RODAK_CHECK_EQ(releases, 0u);
    RODAK_CHECK_EQ(f.input.close_calls.load(), 1u); RODAK_CHECK_EQ(f.focus.releases.load(), 1u);
    RODAK_CHECK_EQ(recording_host::OutstandingRecordingDescriptors(), 0u);
}

RODAK_TEST("invalid channel selection or overflowed sample rate is rejected before focus or files") {
    for (unsigned variant = 0; variant < 5; ++variant) {
        Fixture f; RecordingConfig config;
        if (variant == 0) config.sample_rate = UINT32_MAX;
        if (variant == 1) config.channels = 3;
        if (variant == 2) config.input_channel_mask = 0x10;
        if (variant == 3) config.input_channel_mask = 3;
        if (variant == 4) config.input_channels = 0;
        RODAK_CHECK_FALSE(f.recording->Start(config)); CheckError(f.recording->GetState());
        RODAK_CHECK_EQ(f.focus.requests.load(), 0u); RODAK_CHECK_EQ(f.FileCount(), 0u);
    }
}

RODAK_TEST("active Start Refresh and Delete rejections do not overwrite current recording state") {
    Fixture f; RODAK_CHECK(f.recording->Start());
    RODAK_CHECK(WaitUntil([&]() { return f.recording->GetState().bytes_written >= 4096; }));
    const auto before = f.recording->GetState();
    RODAK_CHECK_FALSE(f.recording->Start());
    RODAK_CHECK_FALSE(f.recording->RefreshRecordings());
    RODAK_CHECK_FALSE(f.recording->DeleteRecording(before.path));
    const auto after = f.recording->GetState();
    RODAK_CHECK_EQ(after.status, RecordingStatus::kRecording);
    RODAK_CHECK_EQ(after.message, "Recording");
    RODAK_CHECK_EQ(after.path, before.path);
    f.recording->Stop();
    RODAK_CHECK_EQ(f.Wait().status, RecordingStatus::kCompleted);
    RODAK_CHECK_EQ(f.focus.requests.load(), 1u);
}

RODAK_TEST("active recording lease rejects external delete and rename of its path") {
    Fixture f; std::promise<void> entered, release; auto gate = release.get_future().share();
    f.input.read_hook = [&](void* bytes, int count) {
        entered.set_value(); gate.wait(); std::memset(bytes, 0, static_cast<size_t>(count)); return true;
    };
    RODAK_CHECK(f.recording->Start());
    entered.get_future().wait();
    const auto state = f.recording->GetState();
    RODAK_CHECK_FALSE(state.path.empty());
    RODAK_CHECK_FALSE(f.files.DeleteFile(state.path));
    RODAK_CHECK_FALSE(f.files.Rename(state.path, state.path + ".moved"));
    release.set_value();
    f.recording->Stop();
    RODAK_CHECK_EQ(f.Wait().status, RecordingStatus::kCompleted);
}

RODAK_TEST("immediate cancellation keeps finalization and cleanup errors visible") {
    for (unsigned failure = 0; failure < 3; ++failure) {
        Fixture f;
        std::promise<void> entered, release;
        auto gate = release.get_future().share();
        f.files.lease_hook = [&]() { entered.set_value(); gate.wait(); };
        StdioFaults fault;
        fault.flush = failure == 0;
        fault.close = failure == 1;
        fault.remove = failure == 2;
        recording_host::SetStdioFaults(fault);
        RODAK_CHECK(f.recording->Start());
        entered.get_future().wait();
        auto stop = std::async(std::launch::async, [&]() { f.recording->Stop(); });
        RODAK_CHECK(WaitUntil([&]() { return f.recording->GetState().status == RecordingStatus::kStopping; }));
        release.set_value();
        stop.get();
        const auto state = f.Wait();
        CheckError(state);
        const char* expected = failure == 0 ? "Cannot flush recording file" :
                               failure == 1 ? "Cannot close recording file" : "Cannot remove cancelled recording";
        RODAK_CHECK(state.message.find(expected) == 0);
        RODAK_CHECK_NE(state.message, "Cancelled");
    }
}

RODAK_TEST("saved file result survives directory refresh failure and list can recover") {
    Fixture f; f.files.fail_list = true;
    const auto saved = f.Capture();
    RODAK_CHECK_EQ(saved.status, RecordingStatus::kCompleted);
    RODAK_CHECK_EQ(saved.message, "Saved");
    RODAK_CHECK_FALSE(saved.library_error.empty());
    RODAK_CHECK(f.recording->GetRecordings().empty());
    f.files.fail_list = false;
    RODAK_CHECK(f.recording->RefreshRecordings());
    RODAK_CHECK(f.recording->GetState().library_error.empty());
    RODAK_CHECK_EQ(f.recording->GetRecordings().size(), 1u);
}
