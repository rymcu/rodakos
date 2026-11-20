// Feeds the shared contracts/rodak-realtime-voice/v1 fixtures to the production parsers and compares the
// device's own control messages with the fixtures, so the schemas cannot drift from the firmware.
#include "phone_os/realtime_voice_contract.h"
#include "phone_os/voice_assistant_transport.h"
#include "test_framework.h"

#include <cJSON.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef RODAK_VOICE_CONTRACT_DIR
#error "RODAK_VOICE_CONTRACT_DIR must point at contracts/rodak-realtime-voice/v1"
#endif

namespace {

using CJsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

std::string ReadFixture(const std::string& relative) {
    std::ifstream input(std::string(RODAK_VOICE_CONTRACT_DIR) + "/examples/" + relative, std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

CJsonPtr ParseFixture(const std::string& relative) {
    const std::string text = ReadFixture(relative);
    return CJsonPtr(cJSON_Parse(text.c_str()), cJSON_Delete);
}

bool ValidServerControl(const std::string& relative) {
    CJsonPtr root = ParseFixture(relative);
    std::string error;
    return root != nullptr && rodakos::ValidateRealtimeVoiceServerControlPayload(root.get(), error);
}

std::vector<uint8_t> HexFixture(const std::string& relative) {
    CJsonPtr root = ParseFixture(relative);
    std::vector<uint8_t> bytes;
    if (!cJSON_IsString(root.get())) return bytes;
    const std::string hex = root->valuestring;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

std::string Key(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) && value->valuestring != nullptr ? value->valuestring : "";
}

}  // namespace

RODAK_TEST("Voice contract fixture: server control events pass the firmware validator") {
    for (const char* fixture :
         {"valid/output-start.reply.json", "valid/output-stop.reply.json", "valid/session-end.goodbye.json",
          "valid/session-end.announce-finished.json", "valid/error.retryable.json",
          "valid/error.no-session.json", "valid/mcp.object.json"}) {
        RODAK_CHECK(ValidServerControl(fixture));
    }
    for (const char* fixture :
         {"invalid/output-stop.empty-reason.json", "invalid/error.no-retryable.json",
          "invalid/mcp.array-payload.json"}) {
        RODAK_CHECK_FALSE(ValidServerControl(fixture));
    }
}

RODAK_TEST("Voice contract fixture: server error fields decode") {
    CJsonPtr root = ParseFixture("valid/error.retryable.json");
    rodakos::RealtimeVoiceServerError server_error;
    std::string error;
    RODAK_CHECK(rodakos::ParseRealtimeVoiceServerError(root.get(), server_error, error));
    RODAK_CHECK(server_error.retryable);
}

RODAK_TEST("Voice contract fixture: RAV1 frames parse and reject as documented") {
    rodakos::RealtimeVoiceAudioFrame frame;
    std::string error;
    for (const char* fixture : {"valid/rav1-frame.opus.json", "valid/rav1-frame.later.json"}) {
        const std::vector<uint8_t> bytes = HexFixture(fixture);
        RODAK_CHECK_FALSE(bytes.empty());
        RODAK_CHECK(rodakos::ParseRealtimeVoiceAudioFrame(bytes.data(), bytes.size(), 8192, frame, error));
    }
    for (const char* fixture : {"invalid/rav1-frame.zero-sequence.json", "invalid/rav1-frame.length-mismatch.json",
                                "invalid/rav1-frame.bad-magic.json"}) {
        const std::vector<uint8_t> bytes = HexFixture(fixture);
        RODAK_CHECK_FALSE(bytes.empty());
        RODAK_CHECK_FALSE(rodakos::ParseRealtimeVoiceAudioFrame(bytes.data(), bytes.size(), 8192, frame, error));
    }
}

RODAK_TEST("Voice contract fixture: device-built messages match the fixtures") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    descriptor.endpoint = "ws://rodak.local:9080/api/v1/aiot/devices/realtime-voice";
    descriptor.vad_strategies = {"device-authoritative", "server-authoritative", "hybrid-fallback"};
    descriptor.preferred_vad_strategy = "hybrid-fallback";

    CJsonPtr built(cJSON_Parse(rodakos::BuildRealtimeVoiceSessionOpenMessage(descriptor, true, 7).c_str()),
                   cJSON_Delete);
    CJsonPtr fixture = ParseFixture("valid/session-open.wake.json");
    RODAK_CHECK(built != nullptr && fixture != nullptr);
    RODAK_CHECK(cJSON_Compare(built.get(), fixture.get(), true));

    CJsonPtr announce(
        cJSON_Parse(rodakos::BuildRealtimeVoiceSessionOpenMessage(descriptor, true, 8, 17).c_str()),
        cJSON_Delete);
    CJsonPtr announce_fixture = ParseFixture("valid/session-open.announce.json");
    RODAK_CHECK(cJSON_Compare(announce.get(), announce_fixture.get(), true));

    CJsonPtr wake(cJSON_Parse(rodakos::BuildRealtimeVoiceWakeMessage("rv-7f3c2a9e1b4d4c6a", "你好达克").c_str()),
                  cJSON_Delete);
    CJsonPtr wake_fixture = ParseFixture("valid/wake-detected.phrase.json");
    RODAK_CHECK(cJSON_Compare(wake.get(), wake_fixture.get(), true));

    CJsonPtr input(cJSON_Parse(rodakos::BuildRealtimeVoiceInputMessage(
                                   rodakos::kRealtimeVoiceEventInputStart, "rv-7f3c2a9e1b4d4c6a", "realtime")
                                   .c_str()),
                   cJSON_Delete);
    CJsonPtr input_fixture = ParseFixture("valid/input-start.realtime.json");
    RODAK_CHECK(cJSON_Compare(input.get(), input_fixture.get(), true));

    CJsonPtr abort(cJSON_Parse(rodakos::BuildRealtimeVoicePlaybackAbortMessage(
                                   "rv-7f3c2a9e1b4d4c6a", rodakos::VoiceAbortReason::kVadDetected, 1)
                                   .c_str()),
                   cJSON_Delete);
    CJsonPtr abort_fixture = ParseFixture("valid/playback-abort.barge-in.json");
    RODAK_CHECK_EQ(Key(abort.get(), "reason"), Key(abort_fixture.get(), "reason"));
    RODAK_CHECK(cJSON_Compare(abort.get(), abort_fixture.get(), true));
}
