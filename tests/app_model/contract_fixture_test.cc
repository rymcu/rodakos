// Feeds the shared contracts/rodak-aiot/v1 fixtures to the production parsers so the
// machine-readable schemas cannot drift from what the firmware actually accepts.
#include "phone_os/device_pairing_protocol.h"
#include "phone_os/realtime_voice_contract.h"
#include "test_framework.h"

#include <cJSON.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#ifndef RODAK_AIOT_CONTRACT_DIR
#error "RODAK_AIOT_CONTRACT_DIR must point at contracts/rodak-aiot/v1"
#endif

namespace {

using rodakos::DevicePairingResponse;
using rodakos::DevicePairingResponseType;
using rodakos::DevicePairingStatus;
using CJsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

std::string ReadFixture(const std::string& relative) {
    std::ifstream input(std::string(RODAK_AIOT_CONTRACT_DIR) + "/examples/" + relative,
                        std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

std::string Envelope(const std::string& data) {
    return R"({"code":200,"message":"OK","data":)" + data + "}";
}

}  // namespace

RODAK_TEST("Contract fixture: created binding request parses") {
    const std::string body = ReadFixture("valid/binding-request-created.manual.json");
    RODAK_CHECK_FALSE(body.empty());
    DevicePairingResponse response;
    std::string error;
    RODAK_CHECK(rodakos::ParseDevicePairingResponse(Envelope(body),
                                                    DevicePairingResponseType::kCreateRequest,
                                                    response, error));
    RODAK_CHECK_EQ(response.request_id, "3f2b8c1e9a4d4f6b8e2a1c5d7b9e0f12");
    RODAK_CHECK_EQ(response.request_token, "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8");
    RODAK_CHECK_EQ(response.pairing_code, "48291307");
    RODAK_CHECK_EQ(response.status, DevicePairingStatus::kPending);
}

RODAK_TEST("Contract fixture: binding status values classify as documented") {
    struct Case {
        const char* fixture;
        DevicePairingStatus status;
    };
    const Case cases[] = {
        {"valid/binding-status.pending.json", DevicePairingStatus::kPending},
        {"valid/binding-status.expired.json", DevicePairingStatus::kExpired},
        {"valid/binding-status.revoked.json", DevicePairingStatus::kRejected},
        {"valid/binding-status.awaiting-device.json", DevicePairingStatus::kUnknown},
        {"valid/binding-status.confirmed.json", DevicePairingStatus::kConfirmed},
    };
    for (const auto& item : cases) {
        const std::string body = ReadFixture(item.fixture);
        RODAK_CHECK_FALSE(body.empty());
        DevicePairingResponse response;
        std::string error;
        RODAK_CHECK(rodakos::ParseDevicePairingResponse(Envelope(body),
                                                        DevicePairingResponseType::kStatus,
                                                        response, error));
        RODAK_CHECK_EQ(response.status, item.status);
    }
}

RODAK_TEST("Contract fixture: Rodak token responses carry a voice descriptor RodakOS accepts") {
    for (const char* fixture : {"valid/token-response.legacy.json", "valid/token-response.pinned.json",
                                "valid/bootstrap.legacy.json"}) {
        const std::string body = ReadFixture(fixture);
        CJsonPtr root(cJSON_Parse(body.c_str()), cJSON_Delete);
        RODAK_CHECK(root != nullptr);
        rodakos::RealtimeVoiceDescriptor descriptor;
        std::string error;
        RODAK_CHECK(rodakos::ParseRealtimeVoiceDescriptor(
            cJSON_GetObjectItemCaseSensitive(root.get(), "realtimeVoice"), descriptor, error));
    }
}

RODAK_TEST("Contract fixture: invalid binding payloads are rejected") {
    DevicePairingResponse response;
    std::string error;
    RODAK_CHECK_FALSE(rodakos::ParseDevicePairingResponse(
        Envelope(ReadFixture("invalid/binding-request-created.no-token.json")),
        DevicePairingResponseType::kCreateRequest, response, error));
    RODAK_CHECK_FALSE(rodakos::ParseDevicePairingResponse(
        Envelope(ReadFixture("invalid/binding-status.empty-status.json")),
        DevicePairingResponseType::kStatus, response, error));
    RODAK_CHECK_FALSE(rodakos::ParseDevicePairingResponse(
        R"({"code":401,"message":"rejected","data":null})",
        DevicePairingResponseType::kStatus, response, error));
}

RODAK_TEST("Contract fixture: realtime voice descriptor parses and rejects legacy auth") {
    const std::string valid = ReadFixture("valid/realtime-voice-descriptor.legacy.json");
    CJsonPtr valid_root(cJSON_Parse(valid.c_str()), cJSON_Delete);
    RODAK_CHECK(valid_root != nullptr);
    rodakos::RealtimeVoiceDescriptor descriptor;
    std::string error;
    RODAK_CHECK(rodakos::ParseRealtimeVoiceDescriptor(valid_root.get(), descriptor, error));
    RODAK_CHECK_EQ(descriptor.downlink_sample_rate_hz, 24000);
    RODAK_CHECK_EQ(descriptor.max_audio_frame_bytes, static_cast<size_t>(8192));
    RODAK_CHECK_EQ(descriptor.preferred_vad_strategy, std::string("hybrid-fallback"));

    const std::string invalid = ReadFixture("invalid/realtime-voice-descriptor.xiaozhi-auth.json");
    CJsonPtr invalid_root(cJSON_Parse(invalid.c_str()), cJSON_Delete);
    RODAK_CHECK(invalid_root != nullptr);
    rodakos::RealtimeVoiceDescriptor rejected;
    RODAK_CHECK_FALSE(rodakos::ParseRealtimeVoiceDescriptor(invalid_root.get(), rejected, error));
}
