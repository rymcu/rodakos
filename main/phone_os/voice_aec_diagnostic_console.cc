#include "phone_os/voice_aec_diagnostic_console.h"
#include "phone_os/voice_audio_frontend.h"

#include <cstdio>
#include <inttypes.h>
#include <charconv>
#include <string_view>

namespace rodakos {
namespace {
bool ReadNumber(std::string_view& args, size_t& value) {
    while (!args.empty() && args.front() == ' ') args.remove_prefix(1);
    if (args.empty()) return false;
    const auto parsed = std::from_chars(args.data(), args.data() + args.size(), value);
    if (parsed.ec != std::errc{} ||
        (parsed.ptr != args.data() + args.size() && *parsed.ptr != ' ')) return false;
    args.remove_prefix(static_cast<size_t>(parsed.ptr - args.data()));
    return true;
}
bool AtEnd(std::string_view args) {
    return args.find_first_not_of(' ') == std::string_view::npos;
}
}

bool HandleVoiceAecDiagnosticCommand(VoiceAudioFrontend& frontend, const std::string& command) {
    const std::string_view text(command);
    const size_t separator = text.find(' ');
    const auto verb = text.substr(0, separator);
    auto args = separator == std::string_view::npos ? std::string_view{} : text.substr(separator + 1);
    if (verb == "aec_arm") {
        size_t duration_ms = 0;
        if (!ReadNumber(args, duration_ms) || !AtEnd(args) || duration_ms > 6000) return false;
        return frontend.ArmAecDiagnosticCapture(static_cast<uint32_t>(duration_ms));
    }
    if (verb == "aec_read") {
        size_t channel = 0, offset = 0, count = 0;
        std::string hex;
        if (!ReadNumber(args, channel) || !ReadNumber(args, offset) || !ReadNumber(args, count) ||
            !AtEnd(args) || channel > 4) return false;
        if (!frontend.ReadAecDiagnosticCaptureChunk(static_cast<uint8_t>(channel), offset, count, hex)) {
            return false;
        }
        std::printf("RODAK_AEC_DATA {\"channel\":%zu,\"offset\":%zu,\"samples\":%zu,\"hex\":\"%s\"}\n",
                    channel, offset, count, hex.c_str());
        return true;
    }
    if (!AtEnd(args)) return false;
    if (verb == "aec_stop") {
        frontend.StopAecDiagnosticCapture();
        return true;
    }
    if (verb == "aec_clear") return frontend.ClearAecDiagnosticCapture();
    if (verb != "aec_status") return false;
    const auto s = frontend.GetAecDiagnosticCaptureStatus();
    std::printf("RODAK_AEC_DATA {\"state\":%d,\"capacity_samples\":%zu,\"raw_samples\":%zu,"
                "\"afe_samples\":%zu,\"generation\":%" PRIu32 ",\"raw_first_sample_us\":%" PRId64
                ",\"afe_first_sample_us\":%" PRId64 ",\"raw_discontinuities\":%" PRIu32
                ",\"afe_discontinuities\":%" PRIu32 ",\"initial_mic_slot\":%d,\"final_mic_slot\":%d,"
                "\"mic_switches\":%" PRIu32 "}\n",
                static_cast<int>(s.state), s.capacity_samples, s.raw_samples, s.afe_samples,
                s.generation, s.raw_first_sample_us, s.afe_first_sample_us, s.raw_discontinuities,
                s.afe_discontinuities, s.initial_mic_slot, s.final_mic_slot, s.mic_switches);
    return true;
}

}  // namespace rodakos
