#include "test_framework.h"
#include "phone_os/voice_mic_selector.h"

#include <vector>

namespace {
using rodakos::VoiceMicSelector;

// One 20 ms read of four-slot TDM with constant-magnitude square waves per slot.
std::vector<int16_t> Read(int16_t mic1, int16_t reference, int16_t mic2, int16_t dc = 0) {
    std::vector<int16_t> tdm(320 * VoiceMicSelector::kChannels);
    for (size_t i = 0; i < 320; ++i) {
        const int sign = i % 2 ? 1 : -1;
        tdm[i * 4] = static_cast<int16_t>(dc + sign * mic1);
        tdm[i * 4 + 1] = static_cast<int16_t>(dc + sign * reference);
        tdm[i * 4 + 2] = static_cast<int16_t>(dc + sign * mic2);
    }
    return tdm;
}

void Feed(VoiceMicSelector& selector, const std::vector<int16_t>& tdm, int reads) {
    for (int i = 0; i < reads; ++i) selector.Update(tdm.data(), tdm.size() / 4);
}
}

RODAK_TEST("mic selector switches to the louder microphone without playback") {
    VoiceMicSelector selector;
    RODAK_CHECK_EQ(selector.selected_mic(), 2);
    RODAK_CHECK_EQ(selector.selected_slot(), size_t{2});
    Feed(selector, Read(1000, 0, 50), 10);
    RODAK_CHECK_EQ(selector.selected_mic(), 1);
    RODAK_CHECK_EQ(selector.selected_slot(), size_t{0});
}

RODAK_TEST("loud loopback requests ADC headroom until a full second without playback") {
    VoiceMicSelector selector;
    auto tdm = Read(30000, 100, 30000);
    for (size_t i = 0; i < 3; ++i) tdm[4 * i + 1] = -32768;
    Feed(selector, tdm, 1);
    RODAK_CHECK_FALSE(selector.needs_adc_headroom());
    tdm[13] = 24576;
    Feed(selector, tdm, 1);
    RODAK_CHECK(selector.needs_adc_headroom());
    Feed(selector, Read(100, 2000, 100), 60);
    RODAK_CHECK(selector.needs_adc_headroom());
    Feed(selector, Read(100, 0, 100), 49);
    RODAK_CHECK(selector.needs_adc_headroom());
    Feed(selector, Read(100, 0, 100), 1);
    RODAK_CHECK_FALSE(selector.needs_adc_headroom());
    Feed(selector, tdm, 1);
    selector.ResetSession();
    RODAK_CHECK_FALSE(selector.needs_adc_headroom());
}

RODAK_TEST("mic selector keeps the microphone while the speaker reference is active") {
    VoiceMicSelector selector;
    // Speaker echo reaches MIC1 louder than MIC2; switching would reset the AEC echo path.
    Feed(selector, Read(1000, 2000, 50), 20);
    RODAK_CHECK(selector.playback_hold());
    RODAK_CHECK_EQ(selector.selected_mic(), 2);
}

RODAK_TEST("mic selector holds through the echo tail then may switch again") {
    VoiceMicSelector selector;
    Feed(selector, Read(1000, 2000, 50), 5);
    // 1 s of hold after the reference goes quiet: 49 more reads still hold.
    Feed(selector, Read(1000, 0, 50), 49);
    RODAK_CHECK(selector.playback_hold());
    RODAK_CHECK_EQ(selector.selected_mic(), 2);
    Feed(selector, Read(1000, 0, 50), 1);
    RODAK_CHECK_FALSE(selector.playback_hold());
    Feed(selector, Read(1000, 0, 50), 10);
    RODAK_CHECK_EQ(selector.selected_mic(), 1);
}

RODAK_TEST("mic selector treats a reference below -60 dBFS as no playback") {
    VoiceMicSelector selector;
    Feed(selector, Read(1000, 31, 50), 10);
    RODAK_CHECK_FALSE(selector.playback_hold());
    RODAK_CHECK_EQ(selector.selected_mic(), 1);
}

RODAK_TEST("mic selector does not lock onto quieter mic during speech ramp-up") {
    VoiceMicSelector selector;
    // First block crosses kSpeechLockPower (27,000 > 25,000) on MIC1 while MIC2 is silent,
    // and even when MIC2 has slight voice below 2x MIC1 during onset, MIC2 must not lock.
    Feed(selector, Read(300, 0, 0), 1);
    Feed(selector, Read(900, 0, 100), 4);
    RODAK_CHECK_EQ(selector.selected_mic(), 1);

    selector.Reset();
    Feed(selector, Read(240, 0, 180), 1);
    Feed(selector, Read(900, 0, 180), 5);
    RODAK_CHECK_EQ(selector.selected_mic(), 1);
}

RODAK_TEST("mic selector switches on softer far-field speech but ignores ambient noise") {
    VoiceMicSelector selector;
    Feed(selector, Read(40, 0, 10), 10);
    RODAK_CHECK_EQ(selector.selected_mic(), 2);

    Feed(selector, Read(200, 0, 50), 6);
    RODAK_CHECK_EQ(selector.selected_mic(), 1);
}

RODAK_TEST("mic selector removes DC offset from reference hold and speech power") {
    VoiceMicSelector selector;
    Feed(selector, Read(0, 0, 0, 600), 10);
    RODAK_CHECK_FALSE(selector.playback_hold());
    RODAK_CHECK_EQ(selector.selected_mic(), 2);

    Feed(selector, Read(400, 0, 50, 600), 6);
    RODAK_CHECK_EQ(selector.selected_mic(), 1);
}

RODAK_TEST("mic selector ResetSession clears hold and lock while keeping chosen mic") {
    VoiceMicSelector selector;
    Feed(selector, Read(600, 0, 50), 6);
    RODAK_CHECK_EQ(selector.selected_mic(), 1);
    Feed(selector, Read(100, 2000, 600), 3);
    RODAK_CHECK(selector.playback_hold());

    selector.ResetSession();
    RODAK_CHECK_FALSE(selector.playback_hold());
    RODAK_CHECK_EQ(selector.selected_mic(), 1);
    Feed(selector, Read(50, 0, 600), 5);
    RODAK_CHECK_EQ(selector.selected_mic(), 2);
}
