#include "test_framework.h"

#include "phone_os/audio_output_service.h"
#include "phone_os/audio_service.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <thread>
#include <vector>

#include <esp_codec_dev.h>

namespace {
using fake_codec::fail_volume_write;
using fake_codec::volume_writes;
int& codec_volume = fake_codec::volume;

void ResetCodec() {
    fake_codec::Reset();
}
}  // namespace

RODAK_TEST("Audio output keeps the last accepted volume when the codec rejects a write") {
    ResetCodec();
    rodakos::AudioOutputService output;
    RODAK_CHECK(output.OpenForOwner("test", 16000, 1, 16));
    RODAK_CHECK_EQ(output.volume(), 60);
    fail_volume_write = true;
    RODAK_CHECK_FALSE(output.SetVolume(35));
    RODAK_CHECK_EQ(output.volume(), 60);
    RODAK_CHECK_EQ(codec_volume, 60);
    RODAK_CHECK(output.IsOpenForOwner("test"));
}

RODAK_TEST("Audio output can retry after a rejected volume without losing its owner") {
    ResetCodec();
    rodakos::AudioOutputService output;
    RODAK_CHECK(output.OpenForOwner("test", 16000, 1, 16));
    RODAK_CHECK(output.SetVolume(20));
    fail_volume_write = true;
    RODAK_CHECK_FALSE(output.SetVolume(90));
    RODAK_CHECK_EQ(output.volume(), 20);
    fail_volume_write = false;
    RODAK_CHECK(output.SetVolume(90));
    RODAK_CHECK_EQ(output.volume(), 90);
    RODAK_CHECK_EQ(codec_volume, 90);
}

RODAK_TEST("Audio output retains closed-codec volume as a setting for the next open") {
    ResetCodec();
    rodakos::AudioOutputService output;
    RODAK_CHECK(output.SetVolume(27));
    RODAK_CHECK_EQ(output.volume(), 27);
    RODAK_CHECK_EQ(volume_writes, 0);
    RODAK_CHECK_EQ(fake_codec::initializations, 0);
    RODAK_CHECK_EQ(fake_codec::opens, 0);
    RODAK_CHECK(output.OpenForOwner("test", 16000, 1, 16));
    RODAK_CHECK_EQ(codec_volume, 27);
}

RODAK_TEST("Audio output only commits a clamped volume after the open codec accepts it") {
    ResetCodec();
    rodakos::AudioOutputService output;
    RODAK_CHECK(output.OpenForOwner("test", 16000, 1, 16));
    RODAK_CHECK(output.SetVolume(150));
    RODAK_CHECK_EQ(output.volume(), 100);
    fail_volume_write = true;
    RODAK_CHECK_FALSE(output.SetVolume(-10));
    RODAK_CHECK_EQ(output.volume(), 100);
    fail_volume_write = false;
    RODAK_CHECK(output.SetVolume(-10));
    RODAK_CHECK_EQ(output.volume(), 0);
    RODAK_CHECK_EQ(codec_volume, 0);
}

RODAK_TEST("Initial codec volume failure closes the codec and releases the output owner") {
    ResetCodec();
    rodakos::AudioOutputService output;
    RODAK_CHECK(output.SetVolume(27));
    RODAK_CHECK(output.ReserveOwner("first"));
    fail_volume_write = true;
    RODAK_CHECK_FALSE(output.OpenForOwner("first", 16000, 1, 16));
    RODAK_CHECK_FALSE(output.IsOpen());
    RODAK_CHECK_FALSE(fake_codec::open);
    RODAK_CHECK_EQ(fake_codec::closes, 1);
    RODAK_CHECK_EQ(fake_codec::deinitializations, 0);
    RODAK_CHECK_EQ(output.volume(), 27);
    fail_volume_write = false;
    RODAK_CHECK(output.OpenForOwner("retry", 16000, 1, 16));
    RODAK_CHECK(output.IsOpenForOwner("retry"));
    RODAK_CHECK_EQ(fake_codec::opens, 2);
    RODAK_CHECK_EQ(fake_codec::initializations, 1);
    RODAK_CHECK_EQ(codec_volume, 27);
}

RODAK_TEST("A same-format volume failure preserves the existing open codec and owner") {
    ResetCodec();
    rodakos::AudioOutputService output;
    RODAK_CHECK(output.OpenForOwner("first", 16000, 1, 16));
    fail_volume_write = true;
    RODAK_CHECK_FALSE(output.OpenForOwner("first", 16000, 1, 16));
    RODAK_CHECK(output.IsOpenForOwner("first"));
    RODAK_CHECK_FALSE(output.ReserveOwner("other"));
    RODAK_CHECK_EQ(fake_codec::opens, 1);
    RODAK_CHECK_EQ(fake_codec::closes, 0);
    RODAK_CHECK_EQ(output.volume(), 60);
    fail_volume_write = false;
    RODAK_CHECK(output.OpenForOwner("first", 16000, 1, 16));
    RODAK_CHECK_EQ(fake_codec::opens, 1);
}

RODAK_TEST("A format-change volume failure closes the newly opened codec and permits retry") {
    ResetCodec();
    rodakos::AudioOutputService output;
    RODAK_CHECK(output.OpenForOwner("first", 16000, 1, 16));
    fail_volume_write = true;
    RODAK_CHECK_FALSE(output.OpenForOwner("first", 48000, 2, 16));
    RODAK_CHECK_FALSE(output.IsOpen());
    RODAK_CHECK_EQ(fake_codec::closes, 2);
    fail_volume_write = false;
    RODAK_CHECK(output.OpenForOwner("retry", 48000, 2, 16));
    RODAK_CHECK_EQ(fake_codec::opens, 3);
}

RODAK_TEST("Audio service keeps playback and UI volumes when the codec rejects a write") {
    ResetCodec();
    rodakos::AudioOutputService output;
    rodakos::AudioService audio(output);
    RODAK_CHECK(output.OpenForOwner("audio-playback", 16000, 1, 16));
    RODAK_CHECK(audio.SetVolume(20));
    fail_volume_write = true;
    RODAK_CHECK_FALSE(audio.SetVolume(90));
    RODAK_CHECK_EQ(audio.volume(), 20);
    RODAK_CHECK_EQ(audio.GetState().volume, 20);
    RODAK_CHECK_EQ(output.volume(), 20);
    RODAK_CHECK_EQ(codec_volume, 20);
    fail_volume_write = false;
    RODAK_CHECK(audio.SetVolume(90));
    RODAK_CHECK_EQ(audio.volume(), 90);
    RODAK_CHECK_EQ(audio.GetState().volume, 90);
    RODAK_CHECK_EQ(output.volume(), 90);
    RODAK_CHECK_EQ(codec_volume, 90);
}

RODAK_TEST("Audio service accepts clamped configuration without opening the codec") {
    ResetCodec();
    rodakos::AudioOutputService output;
    rodakos::AudioService audio(output);
    RODAK_CHECK(audio.SetVolume(150));
    RODAK_CHECK_EQ(audio.volume(), 100);
    RODAK_CHECK_EQ(audio.GetState().volume, 100);
    RODAK_CHECK_EQ(output.volume(), 100);
    RODAK_CHECK_EQ(fake_codec::initializations, 0);
    RODAK_CHECK_EQ(fake_codec::opens, 0);
    RODAK_CHECK_EQ(volume_writes, 0);
    RODAK_CHECK(output.OpenForOwner("audio-playback", 16000, 1, 16));
    RODAK_CHECK_EQ(codec_volume, 100);
    fail_volume_write = true;
    RODAK_CHECK_FALSE(audio.SetVolume(-10));
    RODAK_CHECK_EQ(audio.GetState().volume, 100);
    fail_volume_write = false;
    RODAK_CHECK(audio.SetVolume(-10));
    RODAK_CHECK_EQ(audio.volume(), 0);
    RODAK_CHECK_EQ(audio.GetState().volume, 0);
    RODAK_CHECK_EQ(output.volume(), 0);
    RODAK_CHECK_EQ(codec_volume, 0);
}

RODAK_TEST("Volume receipts distinguish deferred configuration from a codec API application") {
    ResetCodec();
    rodakos::AudioOutputService output;
    const auto configured = output.ApplyVolume(rodakos::AudioVolumeOperation::kSet, 27);
    RODAK_CHECK(configured.accepted);
    RODAK_CHECK_EQ(configured.previous_volume, 60);
    RODAK_CHECK_EQ(configured.volume, 27);
    RODAK_CHECK_EQ(configured.configuration_revision, 1U);
    RODAK_CHECK(configured.application == rodakos::AudioVolumeApplication::kDeferred);
    RODAK_CHECK_EQ(fake_codec::initializations, 0);
    RODAK_CHECK_EQ(fake_codec::opens, 0);
    RODAK_CHECK_EQ(volume_writes, 0);

    RODAK_CHECK(output.OpenForOwner("test", 16000, 1, 16));
    RODAK_CHECK_EQ(codec_volume, 27);
    const auto applied = output.ApplyVolume(rodakos::AudioVolumeOperation::kUp, 3);
    RODAK_CHECK(applied.accepted);
    RODAK_CHECK_EQ(applied.previous_volume, 27);
    RODAK_CHECK_EQ(applied.volume, 30);
    RODAK_CHECK_EQ(applied.configuration_revision, 2U);
    RODAK_CHECK(applied.application == rodakos::AudioVolumeApplication::kCodecApplied);
    RODAK_CHECK_EQ(codec_volume, 30);

    output.CloseForOwner("test");
    const auto deferred = output.ApplyVolume(rodakos::AudioVolumeOperation::kDown, 10);
    RODAK_CHECK(deferred.accepted);
    RODAK_CHECK_EQ(deferred.volume, 20);
    RODAK_CHECK_EQ(deferred.configuration_revision, 3U);
    RODAK_CHECK(deferred.application == rodakos::AudioVolumeApplication::kDeferred);
    RODAK_CHECK_EQ(volume_writes, 2);
}

RODAK_TEST("Rejected volume writes retain their configuration revision and permit a new attempt") {
    ResetCodec();
    rodakos::AudioOutputService output;
    RODAK_CHECK(output.SetVolume(31));
    RODAK_CHECK(output.OpenForOwner("test", 16000, 1, 16));
    fail_volume_write = true;
    const auto failed = output.ApplyVolume(rodakos::AudioVolumeOperation::kUp, 10);
    RODAK_CHECK_FALSE(failed.accepted);
    RODAK_CHECK_EQ(failed.previous_volume, 31);
    RODAK_CHECK_EQ(failed.volume, 31);
    RODAK_CHECK_EQ(failed.configuration_revision, 1U);
    RODAK_CHECK(failed.application == rodakos::AudioVolumeApplication::kUnverified);
    RODAK_CHECK_EQ(output.volume(), 31);
    fail_volume_write = false;
    const auto retry = output.ApplyVolume(rodakos::AudioVolumeOperation::kUp, 10);
    RODAK_CHECK(retry.accepted);
    RODAK_CHECK_EQ(retry.previous_volume, 31);
    RODAK_CHECK_EQ(retry.volume, 41);
    RODAK_CHECK_EQ(retry.configuration_revision, 2U);
}

RODAK_TEST("Strict volume application rejects invalid values without touching the codec") {
    ResetCodec();
    rodakos::AudioOutputService output;
    RODAK_CHECK(output.SetVolume(25));
    RODAK_CHECK(output.OpenForOwner("test", 16000, 1, 16));
    const int writes_before = volume_writes;
    for (const auto operation : {rodakos::AudioVolumeOperation::kSet,
                                 rodakos::AudioVolumeOperation::kUp,
                                 rodakos::AudioVolumeOperation::kDown}) {
        for (const int invalid : {-1, 101, std::numeric_limits<int>::min(),
                                  std::numeric_limits<int>::max()}) {
            const auto result = output.ApplyVolume(operation, invalid);
            RODAK_CHECK_FALSE(result.accepted);
            RODAK_CHECK_EQ(result.volume, 25);
            RODAK_CHECK_EQ(result.configuration_revision, 1U);
            RODAK_CHECK(result.application == rodakos::AudioVolumeApplication::kUnverified);
        }
    }
    RODAK_CHECK_FALSE(output.ApplyVolume(rodakos::AudioVolumeOperation::kUp, 0).accepted);
    RODAK_CHECK_FALSE(output.ApplyVolume(rodakos::AudioVolumeOperation::kDown, 0).accepted);
    RODAK_CHECK_EQ(volume_writes, writes_before);
    RODAK_CHECK_EQ(output.volume(), 25);
}

RODAK_TEST("Relative volume saturates at each boundary and records every accepted request") {
    ResetCodec();
    rodakos::AudioOutputService output;
    const auto maximum = output.ApplyVolume(rodakos::AudioVolumeOperation::kUp, 100);
    RODAK_CHECK(maximum.accepted);
    RODAK_CHECK_EQ(maximum.previous_volume, 60);
    RODAK_CHECK_EQ(maximum.volume, 100);
    const auto same_maximum = output.ApplyVolume(rodakos::AudioVolumeOperation::kUp, 1);
    RODAK_CHECK_EQ(same_maximum.previous_volume, 100);
    RODAK_CHECK_EQ(same_maximum.volume, 100);
    RODAK_CHECK_EQ(same_maximum.configuration_revision, 2U);
    const auto minimum = output.ApplyVolume(rodakos::AudioVolumeOperation::kDown, 100);
    RODAK_CHECK_EQ(minimum.volume, 0);
    const auto same_minimum = output.ApplyVolume(rodakos::AudioVolumeOperation::kDown, 1);
    RODAK_CHECK_EQ(same_minimum.previous_volume, 0);
    RODAK_CHECK_EQ(same_minimum.volume, 0);
    RODAK_CHECK_EQ(same_minimum.configuration_revision, 4U);
}

RODAK_TEST("Concurrent relative volume requests have one ordered configuration history") {
    ResetCodec();
    rodakos::AudioOutputService output;
    RODAK_CHECK(output.SetVolume(0));
    constexpr size_t count = 32;
    std::vector<rodakos::AudioVolumeResult> results(count);
    std::vector<std::thread> workers;
    std::atomic<size_t> ready{0};
    std::atomic<bool> start{false};
    for (size_t index = 0; index < count; ++index) {
        workers.emplace_back([&, index]() {
            ++ready;
            while (!start.load()) std::this_thread::yield();
            results[index] = output.ApplyVolume(rodakos::AudioVolumeOperation::kUp, 1);
        });
    }
    while (ready.load() != count) std::this_thread::yield();
    start.store(true);
    for (auto& worker : workers) worker.join();
    std::sort(results.begin(), results.end(), [](const auto& first, const auto& second) {
        return first.configuration_revision < second.configuration_revision;
    });
    for (size_t index = 0; index < count; ++index) {
        RODAK_CHECK(results[index].accepted);
        RODAK_CHECK_EQ(results[index].previous_volume, static_cast<int>(index));
        RODAK_CHECK_EQ(results[index].volume, static_cast<int>(index + 1));
        RODAK_CHECK_EQ(results[index].configuration_revision, static_cast<uint32_t>(index + 2));
        RODAK_CHECK(results[index].application == rodakos::AudioVolumeApplication::kDeferred);
    }
    RODAK_CHECK_EQ(output.volume(), static_cast<int>(count));
    RODAK_CHECK_EQ(volume_writes, 0);
}

RODAK_TEST("Playback and UI read externally changed volume across initialization and reopen") {
    ResetCodec();
    rodakos::AudioOutputService output;
    rodakos::AudioService audio(output);
    RODAK_CHECK(audio.SetVolume(12));
    RODAK_CHECK(output.ApplyVolume(rodakos::AudioVolumeOperation::kSet, 47).accepted);
    RODAK_CHECK_EQ(audio.volume(), 47);
    RODAK_CHECK_EQ(audio.GetState().volume, 47);
    RODAK_CHECK(audio.Init());
    RODAK_CHECK_EQ(audio.GetState().volume, 47);
    RODAK_CHECK(output.OpenForOwner("audio-playback", 16000, 1, 16));
    RODAK_CHECK_EQ(codec_volume, 47);
    RODAK_CHECK(output.ApplyVolume(rodakos::AudioVolumeOperation::kDown, 7).accepted);
    RODAK_CHECK_EQ(audio.volume(), 40);
    RODAK_CHECK_EQ(audio.GetState().volume, 40);
    fail_volume_write = true;
    RODAK_CHECK_FALSE(output.ApplyVolume(rodakos::AudioVolumeOperation::kSet, 80).accepted);
    RODAK_CHECK_EQ(audio.GetState().volume, 40);
    fail_volume_write = false;
    RODAK_CHECK(audio.ReleasePlaybackHardware());
    RODAK_CHECK(output.OpenForOwner("audio-playback", 16000, 1, 16));
    RODAK_CHECK_EQ(codec_volume, 40);
    RODAK_CHECK_EQ(audio.GetState().volume, 40);
}

RODAK_TEST("Legacy clamped setters share the same revision stream as strict operations") {
    ResetCodec();
    rodakos::AudioOutputService output;
    rodakos::AudioService audio(output);
    RODAK_CHECK(audio.SetVolume(150));
    const auto decreased = output.ApplyVolume(rodakos::AudioVolumeOperation::kDown, 10);
    RODAK_CHECK_EQ(decreased.previous_volume, 100);
    RODAK_CHECK_EQ(decreased.volume, 90);
    RODAK_CHECK_EQ(decreased.configuration_revision, 2U);
    RODAK_CHECK_EQ(audio.GetState().volume, 90);
    RODAK_CHECK(audio.SetVolume(-10));
    const auto increased = output.ApplyVolume(rodakos::AudioVolumeOperation::kUp, 10);
    RODAK_CHECK_EQ(increased.previous_volume, 0);
    RODAK_CHECK_EQ(increased.volume, 10);
    RODAK_CHECK_EQ(increased.configuration_revision, 4U);
}
