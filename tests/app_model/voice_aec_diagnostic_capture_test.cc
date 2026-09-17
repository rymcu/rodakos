#include "test_framework.h"
#include "phone_os/voice_aec_diagnostic_capture.h"
#include <esp_heap_caps.h>
#include <vector>

using rodakos::VoiceAecDiagnosticCapture;
using CaptureState = VoiceAecDiagnosticCapture::State;

RODAK_TEST("AEC capture isolates raw slots and independent AFE stream with bounded reads") {
    VoiceAecDiagnosticCapture capture;
    std::string hex;
    std::vector<int16_t> raw(32 * 4);
    for (size_t i = 0; i < 32; ++i) {
        raw[i * 4] = 0x1234;
        raw[i * 4 + 1] = -1;
        raw[i * 4 + 2] = 0x5678;
        raw[i * 4 + 3] = -32768;
    }
    std::vector<int16_t> afe(32, 0x4321);
    RODAK_CHECK(capture.Arm(1));
    capture.AppendRaw(raw.data(), 32, 7, 5000, 2);
    RODAK_CHECK_EQ(capture.GetStatus().raw_samples, size_t{0});
    capture.Begin(7);
    capture.AppendRaw(raw.data(), 32, 8, 5000, 2);
    RODAK_CHECK_EQ(capture.GetStatus().raw_samples, size_t{0});
    capture.AppendRaw(raw.data(), 32, 7, 5000, 2);
    RODAK_CHECK_EQ(capture.GetStatus().raw_samples, size_t{16});
    RODAK_CHECK_EQ(capture.GetStatus().afe_samples, size_t{0});
    RODAK_CHECK_FALSE(capture.ReadChunk(0, 0, 1, hex));
    capture.AppendAfe(afe.data(), 32, 7, 9000);
    RODAK_CHECK_EQ(capture.GetStatus().state, CaptureState::kComplete);
    RODAK_CHECK_EQ(capture.GetStatus().raw_first_sample_us, int64_t{3000});
    RODAK_CHECK_EQ(capture.GetStatus().afe_first_sample_us, int64_t{7000});
    const char* expected[] = {"3412", "ffff", "7856", "0080", "2143"};
    for (uint8_t slot = 0; slot < 5; ++slot) {
        RODAK_CHECK(capture.ReadChunk(slot, 15, 1, hex));
        RODAK_CHECK_EQ(hex, std::string(expected[slot]));
    }
    RODAK_CHECK_FALSE(capture.ReadChunk(0, 16, 1, hex));
    RODAK_CHECK_FALSE(capture.ReadChunk(5, 0, 1, hex));
    RODAK_CHECK_FALSE(capture.ReadChunk(0, 0, 257, hex));
    RODAK_CHECK_FALSE(capture.ReadChunk(0, static_cast<size_t>(-1), 2, hex));
}

RODAK_TEST("AEC capture cannot overwrite live data and stop retains a partial capture") {
    VoiceAecDiagnosticCapture capture;
    RODAK_CHECK_FALSE(capture.Arm(0));
    RODAK_CHECK_FALSE(capture.Arm(6001));
    RODAK_CHECK(capture.Arm(6000));
    RODAK_CHECK_FALSE(capture.Arm(100));
    RODAK_CHECK_FALSE(capture.Clear());
    capture.Begin(1);
    int16_t raw[] = {1, 2, 3, 4};
    capture.AppendRaw(raw, 1, 1, 1000, 2);
    capture.AppendRaw(raw, 1, 1, 1062, 0);
    capture.MarkDiscontinuity(true, 1);
    capture.MarkDiscontinuity(false, 1);
    capture.MarkDiscontinuity(false, 9);
    RODAK_CHECK_FALSE(capture.Arm(1));
    capture.Stop();
    capture.AppendRaw(raw, 1, 1, 1125, 0);
    RODAK_CHECK_EQ(capture.GetStatus().raw_samples, size_t{2});
    RODAK_CHECK_EQ(capture.GetStatus().mic_switches, uint32_t{1});
    RODAK_CHECK_EQ(capture.GetStatus().raw_discontinuities, uint32_t{1});
    RODAK_CHECK_EQ(capture.GetStatus().afe_discontinuities, uint32_t{1});
    std::string hex;
    RODAK_CHECK(capture.ReadChunk(2, 0, 2, hex));
    RODAK_CHECK_EQ(hex, std::string("03000300"));
    RODAK_CHECK(capture.Clear());
    RODAK_CHECK_EQ(capture.GetStatus().state, CaptureState::kEmpty);
    RODAK_CHECK(capture.Arm(1));
    capture.Stop();
    capture.Begin(2);
    RODAK_CHECK_EQ(capture.GetStatus().state, CaptureState::kStopped);
}

RODAK_TEST("AEC capture allocation failure leaves no armed partial buffer") {
    VoiceAecDiagnosticCapture capture;
    fake_heap_allocations_until_failure = 1;
    const bool armed = capture.Arm(6000);
    fake_heap_allocations_until_failure = -1;
    RODAK_CHECK_FALSE(armed);
    RODAK_CHECK_EQ(capture.GetStatus().state, CaptureState::kEmpty);
    RODAK_CHECK(capture.Arm(6000));
}
