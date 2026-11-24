#include "test_framework.h"
#include "phone_os/voice_mic_selector.h"

#include <vector>

namespace {
using rodakos::VoiceMicSelector;

// One 20 ms read of four-slot TDM with constant-magnitude square waves per slot.
std::vector<int16_t> Read(int16_t mic1, int16_t reference, int16_t mic2) {
    std::vector<int16_t> tdm(320 * VoiceMicSelector::kChannels);
    for (size_t i = 0; i < 320; ++i) {
        const int sign = i % 2 ? 1 : -1;
        tdm[i * 4] = static_cast<int16_t>(sign * mic1);
        tdm[i * 4 + 1] = static_cast<int16_t>(sign * reference);
        tdm[i * 4 + 2] = static_cast<int16_t>(sign * mic2);
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
