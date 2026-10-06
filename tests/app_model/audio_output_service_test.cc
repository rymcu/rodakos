#include "test_framework.h"

#include "phone_os/audio_output_service.h"
#include "phone_os/audio_service.h"

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
