#include <iostream>
#include <string>

#include <esp_codec_dev.h>

#include "phone_os/audio_output_service.h"
#include "phone_os/audio_service.h"
#include "phone_os/voice_volume_mcp.h"

namespace {
struct FixtureOptions {
    bool open_codec = false;
    bool fail_write = false;
};

bool ParseOptions(int argc, char** argv, FixtureOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--codec=open") {
            options.open_codec = true;
        } else if (argument == "--codec=closed") {
            options.open_codec = false;
        } else if (argument == "--fail-write") {
            options.fail_write = true;
        } else {
            std::cerr << "Unsupported fixture option: " << argument << '\n';
            return false;
        }
    }
    if (options.fail_write && !options.open_codec) {
        std::cerr << "--fail-write requires --codec=open\n";
        return false;
    }
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    FixtureOptions options;
    if (!ParseOptions(argc, argv, options)) return 2;

    fake_codec::Reset();
    rodakos::AudioOutputService output;
    rodakos::AudioService audio(output);
    if (options.open_codec &&
        !output.OpenForOwner("mcp-conformance", 16000, 1, 16)) {
        std::cerr << "Cannot open the fake codec for conformance\n";
        return 3;
    }
    fake_codec::fail_volume_write = options.fail_write;

    rodakos::VoiceVolumeMcp dispatcher(output);
    if (!dispatcher.Bind(1)) {
        std::cerr << "Cannot bind the fixture MCP session\n";
        return 4;
    }

    std::string request;
    while (std::getline(std::cin, request)) {
        const std::string response = dispatcher.Handle(request, 1);
        if (audio.volume() != output.volume() ||
            audio.GetState().volume != output.volume()) {
            std::cerr << "Playback/UI volume diverged from the shared output\n";
            return 7;
        }
        if (!response.empty()) {
            std::cout << response << '\n' << std::flush;
            if (!std::cout.good()) return 5;
        }
    }
    dispatcher.Stop();
    return std::cin.bad() ? 6 : 0;
}
