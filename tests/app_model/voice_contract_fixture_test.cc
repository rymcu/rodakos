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
          "valid/error.no-session.json"}) {
        RODAK_CHECK(ValidServerControl(fixture));
    }
    for (const char* fixture :
         {"invalid/output-stop.empty-reason.json", "invalid/error.no-retryable.json"}) {
        RODAK_CHECK_FALSE(ValidServerControl(fixture));
    }
}

RODAK_TEST("Voice contract fixture: turn.accepted decodes and rejects as documented") {
    RODAK_CHECK(ValidServerControl("valid/turn-accepted.first.json"));
    RODAK_CHECK(ValidServerControl("valid/control-message.turn-accepted.json"));
    CJsonPtr root = ParseFixture("valid/turn-accepted.first.json");
    uint32_t turn_id = 0;
    std::string error;
    RODAK_CHECK(rodakos::ParseRealtimeVoiceTurnAccepted(root.get(), turn_id, error));
    RODAK_CHECK_EQ(turn_id, 1u);
    for (const char* fixture : {"invalid/turn-accepted.no-turn.json", "invalid/turn-accepted.turn-zero.json",
                                "invalid/turn-accepted.with-text.json"}) {
        RODAK_CHECK_FALSE(ValidServerControl(fixture));
    }
}

RODAK_TEST("Voice contract fixture: turn.progress decodes and rejects as documented") {
    RODAK_CHECK(ValidServerControl("valid/turn-progress.thinking.json"));
    RODAK_CHECK(ValidServerControl("valid/turn-progress.tool.json"));
    RODAK_CHECK(ValidServerControl("valid/control-message.turn-progress.json"));
    CJsonPtr root = ParseFixture("valid/turn-progress.tool.json");
    rodakos::RealtimeVoiceTurnProgress progress;
    std::string error;
    RODAK_CHECK(rodakos::ParseRealtimeVoiceTurnProgress(root.get(), progress, error));
    RODAK_CHECK_EQ(progress.turn_id, 1u);
    RODAK_CHECK_EQ(progress.seq, 2u);
    RODAK_CHECK(progress.stage == rodakos::VoiceTurnStage::kTool);
    RODAK_CHECK_EQ(progress.label, std::string("正在查询天气"));
    root = ParseFixture("valid/turn-progress.thinking.json");
    RODAK_CHECK(rodakos::ParseRealtimeVoiceTurnProgress(root.get(), progress, error));
    RODAK_CHECK(progress.stage == rodakos::VoiceTurnStage::kThinking);
    RODAK_CHECK(progress.label.empty());
    for (const char* fixture :
         {"invalid/turn-progress.blank-label.json", "invalid/turn-progress.long-label.json",
          "invalid/turn-progress.seq-zero.json", "invalid/turn-progress.tool-done.json",
          "invalid/turn-progress.tool-name.json"}) {
        RODAK_CHECK_FALSE(ValidServerControl(fixture));
    }
}

RODAK_TEST("Voice contract fixture: output.caption decodes and rejects as documented") {
    for (const char* fixture : {"valid/output-caption.first-sentence.json",
                                "valid/output-caption.single-frame.json",
                                "valid/control-message.output-caption.json"}) {
        RODAK_CHECK(ValidServerControl(fixture));
    }
    CJsonPtr root = ParseFixture("valid/output-caption.first-sentence.json");
    rodakos::RealtimeVoiceCaption caption;
    std::string error;
    RODAK_CHECK(rodakos::ParseRealtimeVoiceCaption(root.get(), caption, error));
    RODAK_CHECK_EQ(caption.playback_epoch, 1u);
    RODAK_CHECK_EQ(caption.caption_seq, 1u);
    RODAK_CHECK_EQ(caption.first_audio_seq, 1u);
    RODAK_CHECK_EQ(caption.last_audio_seq, 32u);
    RODAK_CHECK_EQ(caption.text, std::string("明天北京晴，气温 6 到 18 度。"));
    for (const char* fixture :
         {"invalid/output-caption.reversed-range.json", "invalid/output-caption.long-text.json",
          "invalid/output-caption.no-epoch.json", "invalid/output-caption.blank-text.json",
          "invalid/output-caption.transcript.json"}) {
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
    // An ordinary session asks for turn.accepted and turn.progress (contract section 6.1).
    CJsonPtr fixture = ParseFixture("valid/session-open.turn-progress.json");
    RODAK_CHECK(built != nullptr && fixture != nullptr);
    RODAK_CHECK(cJSON_Compare(built.get(), fixture.get(), true));

    CJsonPtr announce(
        cJSON_Parse(rodakos::BuildRealtimeVoiceSessionOpenMessage(descriptor, true, 8, 17).c_str()),
        cJSON_Delete);
    CJsonPtr announce_fixture = ParseFixture("valid/session-open.announce.json");
    RODAK_CHECK(cJSON_Compare(announce.get(), announce_fixture.get(), true));

    // A descriptor that lists output_caption makes both session kinds ask for it (section 6.2).
    CJsonPtr caption_open(
        cJSON_Parse(rodakos::BuildRealtimeVoiceSessionOpenMessage(descriptor, true, 7, 0, true).c_str()),
        cJSON_Delete);
    CJsonPtr caption_fixture = ParseFixture("valid/session-open.caption.json");
    RODAK_CHECK(cJSON_Compare(caption_open.get(), caption_fixture.get(), true));
    CJsonPtr announce_caption(
        cJSON_Parse(rodakos::BuildRealtimeVoiceSessionOpenMessage(descriptor, true, 8, 17, true).c_str()),
        cJSON_Delete);
    CJsonPtr announce_caption_fixture = ParseFixture("valid/session-open.announce-caption.json");
    RODAK_CHECK(cJSON_Compare(announce_caption.get(), announce_caption_fixture.get(), true));

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
