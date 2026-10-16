#include "test_framework.h"
#include "phone_os/pcm_level_meter.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
using rodakos::PcmLevelMeter;

bool Near(float actual, float expected) { return std::fabs(actual - expected) < 0.05f; }
}

RODAK_TEST("pcm level meter reports silence for no samples and all-zero samples") {
    PcmLevelMeter meter;
    RODAK_CHECK(Near(meter.PeakDbfs(), PcmLevelMeter::kSilenceDbfs));
    RODAK_CHECK(Near(meter.RmsDbfs(), PcmLevelMeter::kSilenceDbfs));
    const std::vector<int16_t> zeros(480, 0);
    meter.Add(zeros.data(), zeros.size());
    RODAK_CHECK_EQ(meter.samples(), uint64_t{480});
    RODAK_CHECK(Near(meter.PeakDbfs(), PcmLevelMeter::kSilenceDbfs));
    RODAK_CHECK(Near(meter.RmsDbfs(), PcmLevelMeter::kSilenceDbfs));
}

RODAK_TEST("pcm level meter measures a half-scale square wave across frames") {
    PcmLevelMeter meter;
    std::vector<int16_t> frame(960);
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = (i / 8) % 2 ? -16384 : 16384;
    meter.Add(frame.data(), frame.size());
    meter.Add(frame.data(), frame.size());
    RODAK_CHECK_EQ(meter.peak(), 16384);
    RODAK_CHECK(Near(meter.PeakDbfs(), -6.02f));
    RODAK_CHECK(Near(meter.RmsDbfs(), -6.02f));
}

RODAK_TEST("pcm level meter handles int16 minimum without overflow and resets per reply") {
    PcmLevelMeter meter;
    const int16_t extreme[] = {std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max()};
    meter.Add(extreme, 2);
    RODAK_CHECK_EQ(meter.peak(), 32768);
    RODAK_CHECK(Near(meter.PeakDbfs(), 0.0f));
    RODAK_CHECK(meter.RmsDbfs() <= 0.0f);
    meter.Reset();
    RODAK_CHECK_EQ(meter.samples(), uint64_t{0});
    RODAK_CHECK(Near(meter.RmsDbfs(), PcmLevelMeter::kSilenceDbfs));
    meter.Add(nullptr, 10);
    RODAK_CHECK_EQ(meter.samples(), uint64_t{0});
}
