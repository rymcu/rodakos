#include "test_framework.h"

#include "rodakos_adapters/audio_codec_input.h"

#include <cmath>
#include <cstdint>

#include <esp_codec_dev.h>

RODAK_TEST("AudioCodecInput transfers matching format ownership without closing ES7210 or rewriting matching gain") {
    fake_codec::Reset();
    rodakos::AudioCodecInput input;
    RODAK_CHECK(input.OpenForOwner("voice-wake-frontend", 10, 16000, 4, 16, 30, 0,
                                   rodakos::AudioCodecInput::InputGainProfile::kAecReference10Db));
    RODAK_CHECK_EQ(fake_codec::opens, 1);
    RODAK_CHECK_EQ(fake_codec::closes, 0);
    // MIC3 (ADC channel 2) must never transient through global 30 dB gain.
    RODAK_CHECK_EQ(fake_codec::in_gain_writes, 0);
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes.size(), size_t{2});
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes[0].first, uint16_t{0xB});
    RODAK_CHECK(std::fabs(fake_codec::in_channel_gain_writes[0].second - 30.0f) < 0.001f);
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes[1].first, uint16_t{0x4});
    RODAK_CHECK(std::fabs(fake_codec::in_channel_gain_writes[1].second - 10.0f) < 0.001f);

    // Conversation handoff with same format and profile adopts stream without CloseLocked() or I2C gain writes.
    RODAK_CHECK(input.OpenForOwner("voice-conversation-frontend", 30, 16000, 4, 16, 30, 0,
                                   rodakos::AudioCodecInput::InputGainProfile::kAecReference10Db));
    RODAK_CHECK_EQ(fake_codec::opens, 1);
    RODAK_CHECK_EQ(fake_codec::closes, 0);
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes.size(), size_t{2});

    int16_t buf[16]{};
    RODAK_CHECK_FALSE(input.ReadForOwner("voice-wake-frontend", buf, sizeof(buf)));
    RODAK_CHECK(input.ReadForOwner("voice-conversation-frontend", buf, sizeof(buf)));

    // Different channel_mask (e.g. RecorderApp slot 2) closes and reopens cleanly.
    RODAK_CHECK(input.OpenForOwner("recorder", 40, 16000, 4, 16, 30, 1U << 2,
                                   rodakos::AudioCodecInput::InputGainProfile::kUniform));
    RODAK_CHECK_EQ(fake_codec::closes, 1);
    RODAK_CHECK_EQ(fake_codec::opens, 2);
    RODAK_CHECK_EQ(fake_codec::in_gain_writes, 1);
    input.CloseForOwner("recorder");
    // Closing uniform gain resets cached mic_gain to 0 dB so a later reopen does not replay 30 dB onto MIC3.
    RODAK_CHECK(std::fabs(fake_codec::last_in_gain_db) < 0.001f);
}

RODAK_TEST("AudioCodecInput closes and rejects owner when MIC3 reference channel gain write fails") {
    fake_codec::Reset();
    fake_codec::fail_in_channel_gain_mask = 0x4;
    rodakos::AudioCodecInput input;
    RODAK_CHECK_FALSE(input.OpenForOwner("voice-conversation-frontend", 30, 16000, 4, 16, 30, 0,
                                         rodakos::AudioCodecInput::InputGainProfile::kAecReference10Db));
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes.size(), size_t{2});
    RODAK_CHECK_FALSE(input.IsOpen());
    RODAK_CHECK_EQ(fake_codec::closes, 1);
}

RODAK_TEST("duplex headroom changes ES7210 gains in place and restores normal capture gain") {
    using Profile = rodakos::AudioCodecInput::InputGainProfile;
    fake_codec::Reset();
    rodakos::AudioCodecInput input;
    auto open = [&](int gain, Profile profile) {
        return input.OpenForOwner("voice-conversation-frontend", 30, 16000, 4, 16, gain, 0, profile);
    };
    RODAK_CHECK(open(30, Profile::kAecReference10Db));
    RODAK_CHECK(open(24, Profile::kAecReference0Db));
    RODAK_CHECK(open(24, Profile::kAecReference0Db));
    RODAK_CHECK(open(30, Profile::kAecReference10Db));
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes.size(), size_t{6});
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes[2].first, uint16_t{0xB});
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes[2].second, 24.0f);
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes[3].first, uint16_t{0x4});
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes[3].second, 0.0f);
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes[4].second, 30.0f);
    RODAK_CHECK_EQ(fake_codec::in_channel_gain_writes[5].second, 10.0f);
    RODAK_CHECK_EQ(fake_codec::opens, 1);
    RODAK_CHECK_EQ(fake_codec::closes, 0);
}
