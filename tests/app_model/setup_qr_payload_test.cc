#include "test_framework.h"

#include "phone_os/setup_qr_payload.h"

#include <string>

namespace {

using rodakos::ParseSetupQr;
using rodakos::SetupQrError;
using rodakos::SetupQrErrorCode;
using rodakos::SetupQrKind;
using rodakos::SetupQrPayload;

constexpr char kBootstrapPath[] = "/api/v1/aiot/devices/bootstrap";

// Compare error codes as strings so a failure names the code, not an enum ordinal.
std::string ParseCode(const std::string& text, SetupQrPayload& payload) {
    return SetupQrErrorCode(ParseSetupQr(text, payload));
}

std::string ParseCode(const std::string& text) {
    SetupQrPayload payload;
    return ParseCode(text, payload);
}

std::string RodakQr(const std::string& url) {
    return "RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:" + url + ";;";
}

}  // namespace

// Vectors 1-13 mirror the table in docs/qr-provisioning.md; keep them in the same order.

RODAK_TEST("Setup QR vector 1 parses WiFi and a domain origin") {
    SetupQrPayload payload;
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;", payload),
        std::string("ok"));
    RODAK_CHECK(payload.kind == SetupQrKind::kRodak);
    RODAK_CHECK_EQ(payload.ssid, std::string("HomeNet"));
    RODAK_CHECK_EQ(payload.password, std::string("s3cret-pass"));
    RODAK_CHECK_FALSE(payload.open_network);
    RODAK_CHECK_EQ(payload.bootstrap_url, std::string("https://ic.example.com") + kBootstrapPath);
}

RODAK_TEST("Setup QR vector 2 unescapes the SSID and keeps a custom port") {
    SetupQrPayload payload;
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:Cafe\\;2F;T:nopass;"
                  "U:https://IC.Example.com:8443/api/v1/aiot/devices/bootstrap;;",
                  payload),
        std::string("ok"));
    RODAK_CHECK_EQ(payload.ssid, std::string("Cafe;2F"));
    RODAK_CHECK(payload.open_network);
    RODAK_CHECK(payload.password.empty());
    RODAK_CHECK_EQ(payload.bootstrap_url,
                   std::string("https://ic.example.com:8443") + kBootstrapPath);
}

RODAK_TEST("Setup QR vector 3 accepts any field order and drops port 443") {
    SetupQrPayload payload;
    RODAK_CHECK_EQ(
        ParseCode("RODAK:U:https://ic.example.com:443/;T:wpa;P:s3cret-pass;S:HomeNet;V:1;;",
                  payload),
        std::string("ok"));
    RODAK_CHECK_EQ(payload.bootstrap_url, std::string("https://ic.example.com") + kBootstrapPath);
}

RODAK_TEST("Setup QR vector 4 rejects plain HTTP") {
    RODAK_CHECK_EQ(ParseCode(RodakQr("http://ic.example.com")), std::string("qr_url_insecure"));
}

RODAK_TEST("Setup QR vector 5 rejects an IP address host") {
    RODAK_CHECK_EQ(ParseCode(RodakQr("https://192.168.1.10")), std::string("qr_url_invalid"));
}

RODAK_TEST("Setup QR vector 6 rejects the legacy XiaoZhi path") {
    RODAK_CHECK_EQ(ParseCode(RodakQr("https://ic.example.com/xiaozhi/ota/")),
                   std::string("qr_url_legacy"));
}

RODAK_TEST("Setup QR vector 7 rejects an unknown format version") {
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:2;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
        std::string("qr_version_unsupported"));
}

RODAK_TEST("Setup QR vector 8 rejects a WPA password shorter than 8 bytes") {
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:HomeNet;T:WPA;P:short;U:https://ic.example.com;;"),
                   std::string("qr_password_invalid"));
}

RODAK_TEST("Setup QR vector 9 rejects a duplicate field") {
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:HomeNet;S:Other;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
        std::string("qr_syntax_invalid"));
}

RODAK_TEST("Setup QR vector 10 rejects WEP") {
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:HomeNet;T:WEP;P:s3cret-pass;U:https://ic.example.com;;"),
        std::string("qr_auth_unsupported"));
}

RODAK_TEST("Setup QR vector 11 requires the terminating empty field") {
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com"),
        std::string("qr_syntax_invalid"));
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;"),
        std::string("qr_syntax_invalid"));
}

RODAK_TEST("Setup QR vector 12 accepts a standard WiFi code with a quoted SSID") {
    SetupQrPayload payload;
    RODAK_CHECK_EQ(ParseCode("WIFI:T:WPA;S:\"HomeNet\";P:s3cret-pass;;", payload),
                   std::string("ok"));
    RODAK_CHECK(payload.kind == SetupQrKind::kWifiOnly);
    RODAK_CHECK_EQ(payload.ssid, std::string("HomeNet"));
    RODAK_CHECK_EQ(payload.password, std::string("s3cret-pass"));
    RODAK_CHECK(payload.bootstrap_url.empty());
}

RODAK_TEST("Setup QR vector 13 rejects a bare URL") {
    RODAK_CHECK_EQ(ParseCode("https://ic.example.com"), std::string("qr_not_provisioning"));
}

RODAK_TEST("Setup QR treats quotes literally in RODAK payloads") {
    SetupQrPayload payload;
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:\"Home\";T:WPA;P:s3cret-pass;U:https://ic.example.com;;", payload),
        std::string("ok"));
    RODAK_CHECK_EQ(payload.ssid, std::string("\"Home\""));
}

RODAK_TEST("Setup QR keeps an escaped quote in a WiFi code") {
    SetupQrPayload payload;
    RODAK_CHECK_EQ(ParseCode("WIFI:S:\\\"Home\\\";T:nopass;;", payload), std::string("ok"));
    RODAK_CHECK_EQ(payload.ssid, std::string("\"Home\""));
}

RODAK_TEST("Setup QR keeps colons after the field name in a value") {
    SetupQrPayload payload;
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:Net:5G;T:WPA;P:a:b\\:c\\,d\\\\e;U:https://ic.example.com;;",
                  payload),
        std::string("ok"));
    RODAK_CHECK_EQ(payload.ssid, std::string("Net:5G"));
    RODAK_CHECK_EQ(payload.password, std::string("a:b:c,d\\e"));
}

RODAK_TEST("Setup QR rejects unknown escapes and a trailing backslash") {
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:Home\\nNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
        std::string("qr_syntax_invalid"));
    RODAK_CHECK_EQ(ParseCode("WIFI:S:HomeNet;T:nopass;P:\\"), std::string("qr_syntax_invalid"));
}

RODAK_TEST("Setup QR rejects malformed field names") {
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;s:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
        std::string("qr_syntax_invalid"));
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;SS:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
        std::string("qr_syntax_invalid"));
    RODAK_CHECK_EQ(ParseCode("RODAK:"), std::string("qr_syntax_invalid"));
}

RODAK_TEST("Setup QR ignores unknown fields but still rejects their duplicates") {
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:HomeNet;H:true;X:future;T:WPA;P:s3cret-pass;"
                             "U:https://ic.example.com;;"),
                   std::string("ok"));
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:HomeNet;X:a;X:b;T:WPA;P:s3cret-pass;"
                             "U:https://ic.example.com;;"),
                   std::string("qr_syntax_invalid"));
}

RODAK_TEST("Setup QR prefixes are case-sensitive") {
    RODAK_CHECK_EQ(
        ParseCode("rodak:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
        std::string("qr_not_provisioning"));
}

RODAK_TEST("Setup QR enforces the payload size limit") {
    const std::string base = "RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;";
    std::string at_limit = base + "X:";
    at_limit += std::string(rodakos::kSetupQrMaxPayloadBytes - at_limit.size() - 2, 'a') + ";;";
    RODAK_CHECK_EQ(at_limit.size(), rodakos::kSetupQrMaxPayloadBytes);
    RODAK_CHECK_EQ(ParseCode(at_limit), std::string("ok"));

    std::string over_limit = base + "X:";
    over_limit += std::string(rodakos::kSetupQrMaxPayloadBytes - over_limit.size() - 1, 'a') + ";;";
    RODAK_CHECK_EQ(ParseCode(over_limit), std::string("qr_syntax_invalid"));
}

RODAK_TEST("Setup QR validates the version field") {
    RODAK_CHECK_EQ(ParseCode("RODAK:S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
                   std::string("qr_version_unsupported"));
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:01;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
        std::string("qr_version_unsupported"));
}

RODAK_TEST("Setup QR validates the SSID") {
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
                   std::string("qr_ssid_invalid"));
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
                   std::string("qr_ssid_invalid"));
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:" + std::string(32, 'n') +
                             ";T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
                   std::string("ok"));
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:" + std::string(33, 'n') +
                             ";T:WPA;P:s3cret-pass;U:https://ic.example.com;;"),
                   std::string("qr_ssid_invalid"));
    RODAK_CHECK_EQ(
        ParseCode(std::string("RODAK:V:1;S:Home\x01Net;T:WPA;P:s3cret-pass;") +
                  "U:https://ic.example.com;;"),
        std::string("qr_ssid_invalid"));
}

RODAK_TEST("Setup QR keeps a UTF-8 SSID byte-for-byte") {
    SetupQrPayload payload;
    const std::string ssid = "\xE5\xAE\xB6\xE9\x87\x8C";
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:" + ssid +
                                 ";T:WPA;P:s3cret-pass;U:https://ic.example.com;;",
                             payload),
                   std::string("ok"));
    RODAK_CHECK_EQ(payload.ssid, ssid);
}

RODAK_TEST("Setup QR accepts WPA spellings and requires T in RODAK payloads") {
    for (const char* auth : {"WPA", "wpa2", "WPA3", "sae"}) {
        RODAK_CHECK_EQ(ParseCode(std::string("RODAK:V:1;S:HomeNet;T:") + auth +
                                 ";P:s3cret-pass;U:https://ic.example.com;;"),
                       std::string("ok"));
    }
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:HomeNet;P:s3cret-pass;U:https://ic.example.com;;"),
                   std::string("qr_auth_unsupported"));
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:HomeNet;T:WPA2-EAP;P:s3cret-pass;U:https://ic.example.com;;"),
        std::string("qr_auth_unsupported"));
}

RODAK_TEST("Setup QR validates password length against the auth mode") {
    const auto with_password = [](const std::string& password) {
        return ParseCode("RODAK:V:1;S:HomeNet;T:WPA;P:" + password +
                         ";U:https://ic.example.com;;");
    };
    RODAK_CHECK_EQ(with_password(std::string(8, 'p')), std::string("ok"));
    RODAK_CHECK_EQ(with_password(std::string(63, 'p')), std::string("ok"));
    RODAK_CHECK_EQ(with_password(std::string(64, 'a')), std::string("ok"));
    RODAK_CHECK_EQ(with_password(std::string(64, 'p')), std::string("qr_password_invalid"));
    RODAK_CHECK_EQ(with_password(std::string(65, 'a')), std::string("qr_password_invalid"));
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:HomeNet;T:WPA;U:https://ic.example.com;;"),
                   std::string("qr_password_invalid"));
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:HomeNet;T:nopass;P:s3cret-pass;U:https://ic.example.com;;"),
        std::string("qr_password_invalid"));
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:HomeNet;T:nopass;P:;U:https://ic.example.com;;"),
                   std::string("ok"));
}

RODAK_TEST("Setup QR WiFi codes default to an open network") {
    SetupQrPayload payload;
    RODAK_CHECK_EQ(ParseCode("WIFI:S:Guest;;", payload), std::string("ok"));
    RODAK_CHECK(payload.open_network);
    RODAK_CHECK(payload.kind == SetupQrKind::kWifiOnly);
    RODAK_CHECK_EQ(ParseCode("WIFI:S:Guest;P:s3cret-pass;;"), std::string("qr_password_invalid"));
}

RODAK_TEST("Setup QR WiFi codes ignore a URL field") {
    SetupQrPayload payload;
    RODAK_CHECK_EQ(ParseCode("WIFI:S:Guest;U:http://evil.example;;", payload), std::string("ok"));
    RODAK_CHECK(payload.bootstrap_url.empty());
}

RODAK_TEST("Setup QR requires a URL in RODAK payloads") {
    RODAK_CHECK_EQ(ParseCode("RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;;"),
                   std::string("qr_url_invalid"));
}

RODAK_TEST("Setup QR rejects hosts that the public CA bundle cannot verify") {
    for (const char* url : {"https://localhost", "https://rodak-0123456789abcdef.local",
                            "https://[fd00::1]", "https://10.0.0.1:9443", "https://1.2.3",
                            "https://ic..example.com", "https://-ic.example.com",
                            "https://ic.example.com.", "https://ic_1.example.com"}) {
        RODAK_CHECK_EQ(ParseCode(RodakQr(url)), std::string("qr_url_invalid"));
    }
    RODAK_CHECK_EQ(ParseCode(RodakQr("https://xn--fiqs8s.example.cn")), std::string("ok"));
}

RODAK_TEST("Setup QR rejects URL shapes outside the bootstrap contract") {
    for (const char* url :
         {"https://user@ic.example.com", "https://ic.example.com/?a=1",
          "https://ic.example.com/#frag", "https://ic.example.com/ic/",
          "https://ic.example.com:0", "https://ic.example.com:65536",
          "https://ic.example.com:", "ftp://ic.example.com", "HTTPS://ic.example.com",
          "ic.example.com", "https://"}) {
        RODAK_CHECK_EQ(ParseCode(RodakQr(url)), std::string("qr_url_invalid"));
    }
    RODAK_CHECK_EQ(ParseCode(RodakQr("https://ic.example.com:65535")), std::string("ok"));
    RODAK_CHECK_EQ(ParseCode(RodakQr("HTTP://ic.example.com")), std::string("qr_url_insecure"));
}

RODAK_TEST("Setup QR enforces the URL length limit") {
    // "https://" (8) + three 63-byte labels with dots (192) + label + ".com" (4).
    const auto url_of_size = [](size_t total) {
        const std::string labels = std::string(63, 'a') + "." + std::string(63, 'b') + "." +
                                   std::string(63, 'c') + ".";
        return "https://" + labels + std::string(total - 8 - labels.size() - 4, 'd') + ".com";
    };
    const std::string at_limit = url_of_size(rodakos::kSetupQrMaxUrlBytes);
    RODAK_CHECK_EQ(at_limit.size(), rodakos::kSetupQrMaxUrlBytes);
    RODAK_CHECK_EQ(ParseCode(RodakQr(at_limit)), std::string("ok"));
    RODAK_CHECK_EQ(ParseCode(RodakQr(url_of_size(rodakos::kSetupQrMaxUrlBytes + 1))),
                   std::string("qr_url_invalid"));
}

RODAK_TEST("Setup QR rejects legacy service hosts") {
    RODAK_CHECK_EQ(ParseCode(RodakQr("https://api.tenclass.net")), std::string("qr_url_legacy"));
    RODAK_CHECK_EQ(ParseCode(RodakQr("https://TENCLASS.net")), std::string("qr_url_legacy"));
    RODAK_CHECK_EQ(ParseCode(RodakQr("https://nottenclass.net")), std::string("ok"));
    RODAK_CHECK_EQ(ParseCode(RodakQr("https://ic.example.com/XiaoZhi/OTA/")),
                   std::string("qr_url_legacy"));
}

RODAK_TEST("Setup QR clears the payload on failure") {
    SetupQrPayload payload;
    payload.ssid = "stale";
    payload.password = "stale-password";
    payload.bootstrap_url = "https://stale.example.com";
    RODAK_CHECK_EQ(
        ParseCode("RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:http://ic.example.com;;", payload),
        std::string("qr_url_insecure"));
    RODAK_CHECK(payload.ssid.empty());
    RODAK_CHECK(payload.password.empty());
    RODAK_CHECK(payload.bootstrap_url.empty());
}

RODAK_TEST("Setup QR error codes are stable") {
    RODAK_CHECK_EQ(std::string(SetupQrErrorCode(SetupQrError::kNone)), std::string("ok"));
    RODAK_CHECK_EQ(std::string(SetupQrErrorCode(SetupQrError::kServerPinned)),
                   std::string("qr_server_pinned"));
}
