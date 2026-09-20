#include "test_framework.h"

#include "phone_os/device_cloud_config.h"
#include "phone_os/serial_provisioning_protocol.h"

#include <string>

namespace {

using rodakos::SerialProvisioningFrameAccumulator;
using rodakos::SerialProvisioningFrameResult;

SerialProvisioningFrameResult Feed(const std::string& bytes, std::string& line) {
    SerialProvisioningFrameAccumulator accumulator;
    SerialProvisioningFrameResult result = SerialProvisioningFrameResult::kNeedMore;
    for (const unsigned char byte : bytes) {
        result = accumulator.Push(byte, line);
    }
    return result;
}

}  // namespace

RODAK_TEST("Rodak BigSmart cloud identity is independent from Board Manager naming") {
    RODAK_CHECK_EQ(std::string(rodakos::kRodakBigSmartProductKey), "rymcu-bigsmart");
    RODAK_CHECK_EQ(std::string(rodakos::kRodakAiotProtocol), "rodak-aiot");
    RODAK_CHECK_EQ(rodakos::kRodakAiotProtocolVersion, 1);
}

RODAK_TEST("Serial provisioning accepts a frame exactly at the wire limit") {
    const size_t line_bytes = rodakos::kSerialProvisioningMaxFrameBytes - 1;
    const std::string frame =
        std::string(rodakos::kSerialProvisioningFramePrefix) +
        std::string(line_bytes - rodakos::kSerialProvisioningFramePrefixBytes, 'a') + '\n';
    std::string line;

    RODAK_CHECK_EQ(Feed(frame, line), SerialProvisioningFrameResult::kLineReady);
    RODAK_CHECK_EQ(line.size(), line_bytes);
    RODAK_CHECK_EQ(line, frame.substr(0, frame.size() - 1));
}

RODAK_TEST("Serial provisioning rejects a frame one byte over the wire limit") {
    const size_t line_bytes = rodakos::kSerialProvisioningMaxFrameBytes;
    const std::string frame =
        std::string(rodakos::kSerialProvisioningFramePrefix) +
        std::string(line_bytes - rodakos::kSerialProvisioningFramePrefixBytes, 'a') + '\n';
    std::string line = "stale";

    RODAK_CHECK_EQ(Feed(frame, line), SerialProvisioningFrameResult::kFrameTooLarge);
    RODAK_CHECK(line.empty());
}

RODAK_TEST("Serial provisioning normalizes CRLF but counts CR on the wire") {
    const std::string frame =
        std::string(rodakos::kSerialProvisioningFramePrefix) + "{}\r\n";
    std::string line;

    RODAK_CHECK_EQ(Feed(frame, line), SerialProvisioningFrameResult::kLineReady);
    RODAK_CHECK_EQ(line, std::string(rodakos::kSerialProvisioningFramePrefix) + "{}");
    RODAK_CHECK_EQ(line.find('\r'), std::string::npos);
}

RODAK_TEST("Serial provisioning preserves embedded CR for validation") {
    const std::string frame =
        std::string(rodakos::kSerialProvisioningFramePrefix) + "{\"x\":\"a\r b\"}\n";
    std::string line;

    RODAK_CHECK_EQ(Feed(frame, line), SerialProvisioningFrameResult::kLineReady);
    RODAK_CHECK(line.find("a\r b") != std::string::npos);
}

RODAK_TEST("Serial provisioning waits for LF after a bare CR") {
    SerialProvisioningFrameAccumulator accumulator;
    std::string line;

    RODAK_CHECK_EQ(accumulator.Push('x', line), SerialProvisioningFrameResult::kNeedMore);
    RODAK_CHECK_EQ(accumulator.Push('\r', line), SerialProvisioningFrameResult::kNeedMore);
    RODAK_CHECK_EQ(accumulator.pending_wire_bytes(), static_cast<size_t>(2));
    RODAK_CHECK_EQ(accumulator.Push('\n', line), SerialProvisioningFrameResult::kLineReady);
    RODAK_CHECK_EQ(line, "x");
}

RODAK_TEST("Serial provisioning recovers after an oversized line") {
    SerialProvisioningFrameAccumulator accumulator;
    std::string line;
    const std::string oversized(
        rodakos::kSerialProvisioningMaxFrameBytes, 'x');

    for (const unsigned char byte : oversized) {
        RODAK_CHECK_EQ(accumulator.Push(byte, line), SerialProvisioningFrameResult::kNeedMore);
    }
    RODAK_CHECK_EQ(accumulator.Push('\n', line),
                   SerialProvisioningFrameResult::kFrameTooLarge);
    const std::string valid = "RODAK_PROVISION_V1 {}\n";
    SerialProvisioningFrameResult result = SerialProvisioningFrameResult::kNeedMore;
    for (const unsigned char byte : valid) {
        result = accumulator.Push(byte, line);
    }
    RODAK_CHECK_EQ(result, SerialProvisioningFrameResult::kLineReady);
    RODAK_CHECK_EQ(line, "RODAK_PROVISION_V1 {}");
}

RODAK_TEST("Serial provisioning handles adjacent frames in one RX chunk") {
    SerialProvisioningFrameAccumulator accumulator;
    std::string line;
    int ready_count = 0;
    const std::string frames = "first\nsecond\n";

    for (const unsigned char byte : frames) {
        if (accumulator.Push(byte, line) == SerialProvisioningFrameResult::kLineReady) {
            ++ready_count;
            if (ready_count == 1) {
                RODAK_CHECK_EQ(line, "first");
            } else if (ready_count == 2) {
                RODAK_CHECK_EQ(line, "second");
            }
        }
    }
    RODAK_CHECK_EQ(ready_count, 2);
}

RODAK_TEST("Serial provisioning accepts HTTP bootstrap authorities") {
    RODAK_CHECK(rodakos::IsValidSerialProvisioningBootstrapUrl(
        "http://192.0.2.154:9080/xiaozhi/ota/"));
    RODAK_CHECK(rodakos::IsValidSerialProvisioningBootstrapUrl(
        "https://example.com/bootstrap?channel=stable"));
    RODAK_CHECK(rodakos::IsValidSerialProvisioningBootstrapUrl(
        "http://[fd00::1]:9080/xiaozhi/ota/"));
}

RODAK_TEST("Serial provisioning rejects unsafe bootstrap authorities") {
    const std::string invalid_urls[] = {
        "HTTP://example.com/",
        "http:///xiaozhi/ota/",
        "http://?target=example.com",
        "https://user:password@example.com/",
        "http://example.com:0/",
        "http://example.com:65536/",
        "http://fd00::1/xiaozhi/ota/",
        "http://example.com\\xiaozhi/ota/",
    };
    for (const std::string& url : invalid_urls) {
        RODAK_CHECK_FALSE(rodakos::IsValidSerialProvisioningBootstrapUrl(url));
    }
}

RODAK_TEST("Serial provisioning rejects literal and escaped JSON NUL bytes") {
    RODAK_CHECK(rodakos::ContainsSerialProvisioningJsonNul("{\"ssid\":\"a\\u0000b\"}"));
    const std::string literal = std::string("{\"ssid\":\"a") + '\0' + "b\"}";
    RODAK_CHECK(rodakos::ContainsSerialProvisioningJsonNul(literal));
    RODAK_CHECK_FALSE(rodakos::ContainsSerialProvisioningJsonNul(
        "{\"ssid\":\"literal \\\\u0000 text\"}"));
}

RODAK_TEST("Device cloud save failures distinguish complete and incomplete rollback") {
    using rodakos::ProvisioningUrlSaveResult;

    RODAK_CHECK_EQ(
        rodakos::ClassifyProvisioningUrlSaveFailure(true, true, true),
        ProvisioningUrlSaveResult::kFailedRolledBack);
    RODAK_CHECK_EQ(
        rodakos::ClassifyProvisioningUrlSaveFailure(false, true, true),
        ProvisioningUrlSaveResult::kStateUncertain);
    RODAK_CHECK_EQ(
        rodakos::ClassifyProvisioningUrlSaveFailure(true, false, true),
        ProvisioningUrlSaveResult::kStateUncertain);
    RODAK_CHECK_EQ(
        rodakos::ClassifyProvisioningUrlSaveFailure(true, true, false),
        ProvisioningUrlSaveResult::kStateUncertain);
}

RODAK_TEST("Serial provisioning keeps pending marker when cloud rollback is incomplete") {
    using rodakos::ProvisioningUrlSaveResult;

    RODAK_CHECK(rodakos::ShouldClearSerialProvisioningPendingAfterCloudSaveFailure(
        true, ProvisioningUrlSaveResult::kFailedRolledBack));
    RODAK_CHECK_FALSE(
        rodakos::ShouldClearSerialProvisioningPendingAfterCloudSaveFailure(
            true, ProvisioningUrlSaveResult::kStateUncertain));
    RODAK_CHECK_FALSE(
        rodakos::ShouldClearSerialProvisioningPendingAfterCloudSaveFailure(
            false, ProvisioningUrlSaveResult::kFailedRolledBack));
}
