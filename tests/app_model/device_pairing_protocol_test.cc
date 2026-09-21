#include "phone_os/device_pairing_protocol.h"
#include "test_framework.h"

#include <string>

namespace {

using rodakos::DevicePairingResponse;
using rodakos::DevicePairingResponseType;
using rodakos::DevicePairingStatus;

bool Parse(const std::string& json, DevicePairingResponseType type,
           DevicePairingResponse& response, std::string& error) {
    return rodakos::ParseDevicePairingResponse(json, type, response, error);
}

}  // namespace

RODAK_TEST("Pairing request parses enveloped camel-case fields") {
    DevicePairingResponse response;
    std::string error;

    RODAK_CHECK(Parse(
        R"({"code":200,"data":{"requestId":"req-1","requestToken":"token-1","pairingCode":"48291307","expiresAt":"2026-09-20T08:00:00Z","status":"pending"}})",
        DevicePairingResponseType::kCreateRequest, response, error));
    RODAK_CHECK_EQ(response.request_id, "req-1");
    RODAK_CHECK_EQ(response.request_token, "token-1");
    RODAK_CHECK_EQ(response.pairing_code, "48291307");
    RODAK_CHECK_EQ(response.expires_at, "2026-09-20T08:00:00Z");
    RODAK_CHECK_EQ(response.status, DevicePairingStatus::kPending);
    RODAK_CHECK(error.empty());
}

RODAK_TEST("Pairing request accepts flat snake-case fields and defaults to pending") {
    DevicePairingResponse response;
    std::string error;

    RODAK_CHECK(Parse(
        R"({"request_id":"req-2","request_token":"token-2","pairing_code":"19420582","expires_at":"later"})",
        DevicePairingResponseType::kCreateRequest, response, error));
    RODAK_CHECK_EQ(response.request_id, "req-2");
    RODAK_CHECK_EQ(response.raw_status, "pending");
    RODAK_CHECK_EQ(response.status, DevicePairingStatus::kPending);
}

RODAK_TEST("Pairing status normalizes approved response") {
    DevicePairingResponse response;
    std::string error;

    RODAK_CHECK(Parse(R"({"data":{"status":"  APPROVED  ","expires_at":"later"}})",
                      DevicePairingResponseType::kStatus, response, error));
    RODAK_CHECK_EQ(response.raw_status, "approved");
    RODAK_CHECK_EQ(response.status, DevicePairingStatus::kConfirmed);
    RODAK_CHECK_EQ(response.expires_at, "later");
}

RODAK_TEST("Pairing parser rejects incomplete create response without overwriting state") {
    DevicePairingResponse response;
    response.request_id = "existing";
    response.pairing_code = "existing-code";
    std::string error;

    RODAK_CHECK_FALSE(Parse(
        R"({"data":{"requestId":"req-3","pairingCode":"12345678"}})",
        DevicePairingResponseType::kCreateRequest, response, error));
    RODAK_CHECK_EQ(response.request_id, "existing");
    RODAK_CHECK_EQ(response.pairing_code, "existing-code");
    RODAK_CHECK_FALSE(error.empty());
}

RODAK_TEST("Pairing parser rejects status response without status") {
    DevicePairingResponse response;
    std::string error;

    RODAK_CHECK_FALSE(Parse(R"({"code":200,"data":{"expiresAt":"later"}})",
                            DevicePairingResponseType::kStatus, response, error));
    RODAK_CHECK_FALSE(error.empty());
}

RODAK_TEST("Pairing parser returns server rejection and malformed JSON errors") {
    DevicePairingResponse response;
    std::string error;

    RODAK_CHECK_FALSE(Parse(R"({"code":409,"message":"request already exists"})",
                            DevicePairingResponseType::kCreateRequest, response, error));
    RODAK_CHECK_EQ(error, "request already exists");

    RODAK_CHECK_FALSE(Parse("not-json", DevicePairingResponseType::kStatus,
                            response, error));
    RODAK_CHECK_FALSE(error.empty());
}

RODAK_TEST("Pairing parser preserves terminal statuses for caller policy") {
    const struct {
        const char* value;
        DevicePairingStatus expected;
    } cases[] = {
        {"expired", DevicePairingStatus::kExpired},
        {"rejected", DevicePairingStatus::kRejected},
        {"revoked", DevicePairingStatus::kRejected},
        {"future-status", DevicePairingStatus::kUnknown},
    };

    for (const auto& test_case : cases) {
        DevicePairingResponse response;
        std::string error;
        const std::string json = std::string("{\"status\":\"") +
                                 test_case.value + "\"}";
        RODAK_CHECK(Parse(json, DevicePairingResponseType::kStatus, response, error));
        RODAK_CHECK_EQ(response.status, test_case.expected);
    }
}
