#include "test_framework.h"
#include "host_runtime.h"
#include "stdio_faults.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/audio_service.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {
using namespace rodakos;
using Bytes = std::vector<uint8_t>;
void Le16(Bytes& bytes, size_t at, uint16_t value) { bytes[at] = value; bytes[at + 1] = value >> 8; }
void Le32(Bytes& bytes, size_t at, uint32_t value) { for (unsigned i = 0; i < 4; ++i) bytes[at + i] = value >> (i * 8); }
void Text(Bytes& bytes, size_t at, const char* text, size_t count) { std::copy(text, text + count, bytes.begin() + at); }
Bytes Wav(size_t samples = 4096, uint16_t channels = 1) {
    Bytes bytes(44 + samples * 2, 0);
    Text(bytes, 0, "RIFF", 4); Le32(bytes, 4, bytes.size() - 8); Text(bytes, 8, "WAVEfmt ", 8);
    Le32(bytes, 16, 16); Le16(bytes, 20, 1); Le16(bytes, 22, channels); Le32(bytes, 24, 16000);
    Le32(bytes, 28, 16000 * channels * 2); Le16(bytes, 32, channels * 2); Le16(bytes, 34, 16);
    Text(bytes, 36, "data", 4); Le32(bytes, 40, samples * 2); return bytes;
}
Bytes Mp3(unsigned frames = 8) {
    // Valid MPEG-1 Layer III mono silence, 128 kbps / 44.1 kHz. Zero side-info
    // describes empty spectral data and uses no reservoir; each frame is 417 bytes.
    Bytes bytes(frames * 417, 0);
    for (unsigned frame = 0; frame < frames; ++frame) {
        const size_t at = frame * 417;
        bytes[at] = 0xff; bytes[at + 1] = 0xfb; bytes[at + 2] = 0x90; bytes[at + 3] = 0xc0;
    }
    return bytes;
}
Bytes Id3(bool footer = false) {
    Bytes tag(10 + 64 + (footer ? 10 : 0), 0xff);
    Text(tag, 0, "ID3", 3); tag[3] = 4; tag[4] = 0; tag[5] = footer ? 0x10 : 0;
    tag[6] = tag[7] = tag[8] = 0; tag[9] = 64;
    if (footer) {
        std::copy(tag.begin(), tag.begin() + 10, tag.end() - 10);
        Text(tag, tag.size() - 10, "3DI", 3);
    }
    return tag;
}
struct File {
    std::filesystem::path path;
    File(const Bytes& bytes, const char* extension) {
        static std::atomic<unsigned> serial{0};
        path = std::filesystem::temp_directory_path() /
            ("rodakos-audio-test-" + std::to_string(getpid()) + "-" + std::to_string(++serial) + extension);
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    ~File() { std::filesystem::remove(path); }
};
struct Fixture {
    AudioOutputService output;
    AudioService audio{output};
    Fixture() { audio_host::ResetReadFault(); audio_host::fail_task_creation = false; audio_host::task_start_hook = {}; }
    ~Fixture() { audio.Deinit(); audio_host::JoinTasks(); audio_host::ResetReadFault(); audio_host::task_start_hook = {}; }
    AudioPlaybackState Wait() {
        for (unsigned i = 0; i < 2000; ++i) {
            const auto state = audio.GetState();
            if (state.status == AudioPlaybackStatus::kCompleted || state.status == AudioPlaybackStatus::kError ||
                state.status == AudioPlaybackStatus::kStopped) {
                audio_host::JoinTasks(); return audio.GetState();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("playback did not terminate");
    }
    AudioPlaybackState Play(const File& file) {
        RODAK_CHECK(audio.PlayFile(file.path.string())); return Wait();
    }
};
void CheckError(const AudioPlaybackState& state) { RODAK_CHECK_EQ(state.status, AudioPlaybackStatus::kError); }
}

RODAK_TEST("WAV completion requires all aligned declared audio bytes") {
    Fixture f; File valid(Wav(), ".wav");
    const auto state = f.Play(valid);
    RODAK_CHECK_EQ(state.status, AudioPlaybackStatus::kCompleted);
    RODAK_CHECK_EQ(state.bytes_played, 8192u);
    RODAK_CHECK_EQ(f.output.written.size(), 8192u);
}

RODAK_TEST("WAV rejects truncated RIFF invalid rates and partial stereo frames before output") {
    for (unsigned change = 0; change < 6; ++change) {
        auto bytes = Wav(4, 2);
        if (change == 0) bytes.pop_back();
        if (change == 1) Le32(bytes, 40, 0xffffffffU);
        if (change == 2) { bytes.resize(50); Le32(bytes, 4, 42); Le32(bytes, 40, 6); }
        if (change == 3) { bytes.resize(44); Le32(bytes, 4, 36); Le32(bytes, 40, 0); }
        if (change == 4) { Le32(bytes, 24, 0x80000000U); Le32(bytes, 28, 0); }
        if (change == 5) Le16(bytes, 32, 2);
        Fixture f; File file(bytes, ".wav"); CheckError(f.Play(file));
        RODAK_CHECK_EQ(f.output.write_calls.load(), 0u);
    }
}

RODAK_TEST("WAV read faults after progress retain error and retry completes") {
    for (bool io_error : {false, true}) {
        Fixture f; File file(Wav(), ".wav");
        audio_host::FailRead(2, io_error, 3);
        const auto state = f.Play(file); CheckError(state);
        RODAK_CHECK_EQ(state.bytes_played, 4096u);
        RODAK_CHECK(state.progress_percent < 100);
        RODAK_CHECK_FALSE(f.output.IsOpenForOwner("audio-playback"));
        RODAK_CHECK_EQ(f.Play(file).status, AudioPlaybackStatus::kCompleted);
    }
}

RODAK_TEST("real Helix decodes generated valid MP3 silence") {
    Fixture f; File file(Mp3(), ".mp3");
    const auto state = f.Play(file);
    RODAK_CHECK_EQ(state.status, AudioPlaybackStatus::kCompleted);
    RODAK_CHECK_EQ(state.sample_rate, 44100u);
    RODAK_CHECK_EQ(f.output.write_calls.load(), 8u);
    RODAK_CHECK_EQ(f.output.written.size(), 8u * 1152u * 2u);
}

RODAK_TEST("MP3 truncated final frame and read error never complete after decoded frames") {
    auto truncated = Mp3(); truncated.resize(truncated.size() - 200);
    Fixture f; File bad(truncated, ".mp3"), valid(Mp3(100), ".mp3");
    CheckError(f.Play(bad));
    RODAK_CHECK(f.output.write_calls.load() > 0u);
    audio_host::FailRead(2, true);
    CheckError(f.Play(valid));
    RODAK_CHECK_FALSE(f.output.IsOpenForOwner("audio-playback"));
    RODAK_CHECK_EQ(f.Play(valid).status, AudioPlaybackStatus::kCompleted);
}

RODAK_TEST("output failure reports asynchronous error then allows WAV and MP3 retry") {
    for (const bool mp3 : {false, true}) {
        Fixture f; File file(mp3 ? Mp3() : Wav(), mp3 ? ".mp3" : ".wav");
        f.output.fail_write = true;
        CheckError(f.Play(file));
        RODAK_CHECK_FALSE(f.output.IsOpenForOwner("audio-playback"));
        f.output.fail_write = false;
        RODAK_CHECK_EQ(f.Play(file).status, AudioPlaybackStatus::kCompleted);
    }
}

RODAK_TEST("Stop admitted during the final output write cannot become Completed") {
    Fixture f; File file(Wav(16), ".wav");
    f.output.write_hook = [&]() { f.audio.Stop(); };
    RODAK_CHECK_EQ(f.Play(file).status, AudioPlaybackStatus::kStopped);
    RODAK_CHECK_FALSE(f.output.IsOpenForOwner("audio-playback"));
}

RODAK_TEST("resume hardware error terminates paused worker and remains retryable") {
    Fixture f; File file(Wav(), ".wav");
    f.output.write_hook = [&]() { f.audio.Pause(); };
    RODAK_CHECK(f.audio.PlayFile(file.path.string()));
    for (unsigned i = 0; i < 1000 && f.audio.GetState().status != AudioPlaybackStatus::kPaused; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    RODAK_CHECK(f.audio.SuspendPlaybackHardware());
    f.output.fail_open = true;
    f.audio.Resume();
    CheckError(f.Wait());
    f.output.fail_open = false; f.output.write_hook = {};
    RODAK_CHECK_EQ(f.Play(file).status, AudioPlaybackStatus::kCompleted);
}

RODAK_TEST("Stop during resume open cannot resurrect a playing state") {
    for (const bool open_fails : {false, true}) {
        Fixture f; File file(Wav(), ".wav");
        f.output.write_hook = [&]() { f.audio.Pause(); };
        RODAK_CHECK(f.audio.PlayFile(file.path.string()));
        for (unsigned i = 0; i < 1000 && f.audio.GetState().status != AudioPlaybackStatus::kPaused; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        RODAK_CHECK(f.audio.SuspendPlaybackHardware());
        f.output.fail_open = open_fails;
        f.output.open_hook = [&]() { f.audio.Stop(); };
        f.audio.Resume();
        RODAK_CHECK_EQ(f.Wait().status, AudioPlaybackStatus::kStopped);
        RODAK_CHECK_FALSE(f.output.IsOpenForOwner("audio-playback"));
    }
}

RODAK_TEST("Deinit retains service and output while a playback write is in flight") {
    Fixture f; File file(Wav(), ".wav");
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    f.output.write_hook = [&]() { entered.set_value(); gate.wait(); };
    RODAK_CHECK(f.audio.PlayFile(file.path.string()));
    entered.get_future().wait();
    auto deinit = std::async(std::launch::async, [&]() { f.audio.Deinit(); });
    const bool waited = deinit.wait_for(std::chrono::milliseconds(120)) == std::future_status::timeout;
    const auto calls = f.output.deinit_calls.load();
    release.set_value(); deinit.get();
    RODAK_CHECK(waited); RODAK_CHECK_EQ(calls, 0u);
    RODAK_CHECK_FALSE(f.audio.IsReady());
}

RODAK_TEST("valid ID3v1 ID3v2 and APEv2 metadata do not masquerade as corrupt MP3 frames") {
    for (unsigned variant = 0; variant < 6; ++variant) {
        auto bytes = Mp3();
        if (variant <= 2) {
            const auto tag = Id3(variant == 2);
            if (variant == 0) bytes.insert(bytes.begin(), tag.begin(), tag.end());
            else bytes.insert(bytes.end(), tag.begin(), tag.end());
        } else {
            if (variant >= 4) {
                Bytes footer(32, 0);
                Text(footer, 0, "APETAGEX", 8); Le32(footer, 8, 2000); Le32(footer, 12, 32);
                if (variant == 5) {
                    Le32(footer, 20, 0xa0000000U);
                    bytes.insert(bytes.end(), footer.begin(), footer.end());
                    Le32(footer, 20, 0x80000000U);
                }
                bytes.insert(bytes.end(), footer.begin(), footer.end());
            }
            Bytes id3v1(128, 0xff); Text(id3v1, 0, "TAG", 3);
            bytes.insert(bytes.end(), id3v1.begin(), id3v1.end());
        }
        Fixture f; File file(bytes, ".mp3");
        RODAK_CHECK_EQ(f.Play(file).status, AudioPlaybackStatus::kCompleted);
        RODAK_CHECK_EQ(f.output.write_calls.load(), 8u);
    }
}

RODAK_TEST("first MP3 reservoir underflow consumes its frame and permits following audio") {
    auto bytes = Mp3(); bytes[4] = 1;
    Fixture f; File file(bytes, ".mp3");
    RODAK_CHECK_EQ(f.Play(file).status, AudioPlaybackStatus::kCompleted);
    RODAK_CHECK_EQ(f.output.write_calls.load(), 7u);
}

RODAK_TEST("MP3 invalid metadata lengths and final partial headers report errors") {
    for (unsigned variant = 0; variant < 4; ++variant) {
        auto bytes = Mp3();
        if (variant == 0) { auto tag = Id3(); tag[9] = 127; bytes.insert(bytes.end(), tag.begin(), tag.end()); }
        if (variant == 1) { bytes.push_back(0xff); bytes.push_back(0xfb); }
        if (variant == 2) bytes[417 + 2] = 0xf0;
        if (variant == 3) { Bytes tail(32, 0); Text(tail, 0, "APETAGEX", 8); Le32(tail, 8, 2000); Le32(tail, 12, 0xffffffffU); bytes.insert(bytes.end(), tail.begin(), tail.end()); }
        Fixture f; File file(bytes, ".mp3"); CheckError(f.Play(file));
        RODAK_CHECK_FALSE(f.output.IsOpenForOwner("audio-playback"));
    }
}

RODAK_TEST("task acceptance file open and codec open failures remain distinguishable and retryable") {
    Fixture f; File file(Wav(), ".wav");
    audio_host::fail_task_creation = true;
    RODAK_CHECK_FALSE(f.audio.PlayFile(file.path.string()));
    CheckError(f.audio.GetState());
    audio_host::fail_task_creation = false;
    RODAK_CHECK(f.audio.PlayFile(file.path.string() + ".missing.wav"));
    CheckError(f.Wait());
    f.output.fail_open = true; CheckError(f.Play(file));
    f.output.fail_open = false;
    RODAK_CHECK_EQ(f.Play(file).status, AudioPlaybackStatus::kCompleted);
}

RODAK_TEST("Pause before MP3 task starts stays paused after metadata and can resume") {
    Fixture f; const auto bytes = Mp3(); File file(bytes, ".mp3");
    std::promise<void> release;
    auto gate = release.get_future().share();
    audio_host::task_start_hook = [gate]() { gate.wait(); };
    RODAK_CHECK(f.audio.PlayFile(file.path.string()));
    f.audio.Pause(); release.set_value();
    for (unsigned i = 0; i < 1000 && f.audio.GetState().data_bytes != bytes.size(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto paused = f.audio.GetState();
    f.audio.Resume();
    RODAK_CHECK_EQ(paused.data_bytes, bytes.size());
    RODAK_CHECK_EQ(paused.status, AudioPlaybackStatus::kPaused);
    RODAK_CHECK_EQ(f.Wait().status, AudioPlaybackStatus::kCompleted);
}

RODAK_TEST("MP3 final incomplete header and side info at the allocation boundary never enter Helix") {
    for (const uint8_t version : {uint8_t{0xfb}, uint8_t{0xfa}, uint8_t{0xf3}, uint8_t{0xe3}}) {
        for (const uint8_t channels : {uint8_t{0xc0}, uint8_t{0x00}}) {
            const int side_bytes = ((version >> 3) & 3) == 3 ? (channels == 0xc0 ? 17 : 32)
                                                              : (channels == 0xc0 ? 9 : 17);
            const int header_bytes = (version & 1) != 0 ? 4 : 6;
            for (const int tail_bytes : {4, header_bytes + side_bytes - 1}) {
                auto frames = Mp3(39);
                Bytes bytes(16384 - frames.size() - tail_bytes, 0);
                bytes.insert(bytes.end(), frames.begin(), frames.end());
                const size_t tail_at = bytes.size();
                bytes.resize(16384, 0);
                bytes[tail_at] = 0xff; bytes[tail_at + 1] = version;
                bytes[tail_at + 2] = 0x90; bytes[tail_at + 3] = channels;
                Fixture f; File file(bytes, ".mp3");
                CheckError(f.Play(file));
                RODAK_CHECK_EQ(f.output.write_calls.load(), 39u);
                RODAK_CHECK_FALSE(f.output.IsOpenForOwner("audio-playback"));
            }
        }
    }
}

RODAK_TEST("valid MP3 frames crossing the input buffer boundary still complete") {
    for (const unsigned tail_bytes : {1u, 2u, 3u, 4u, 20u, 37u}) {
        auto frames = Mp3(40);
        Bytes bytes(16384 - 39 * 417 - tail_bytes, 0);
        bytes.insert(bytes.end(), frames.begin(), frames.end());
        Fixture f; File file(bytes, ".mp3");
        RODAK_CHECK_EQ(f.Play(file).status, AudioPlaybackStatus::kCompleted);
        RODAK_CHECK_EQ(f.output.write_calls.load(), 40u);
    }
}
