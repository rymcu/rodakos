#include "phone_os/setup_qr_payload.h"

#include "phone_os/serial_provisioning_protocol.h"

#include <array>
#include <cctype>
#include <initializer_list>
#include <optional>

namespace rodakos {
namespace {

constexpr std::string_view kRodakPrefix = "RODAK:";
constexpr std::string_view kWifiPrefix = "WIFI:";
constexpr size_t kMaxDomainBytes = 253;
constexpr size_t kMaxLabelBytes = 63;

struct Field {
    std::optional<std::string> value;
    // Standard WIFI: codes may quote a value. Only unescaped quotes at both ends count.
    bool quoted = false;
};

struct Fields {
    Field version;
    Field ssid;
    Field auth;
    Field password;
    Field url;
};

Field* SelectField(Fields& fields, char name) {
    switch (name) {
        case 'V': return &fields.version;
        case 'S': return &fields.ssid;
        case 'T': return &fields.auth;
        case 'P': return &fields.password;
        case 'U': return &fields.url;
        default: return nullptr;
    }
}

bool IsEscapable(char character) {
    return character == '\\' || character == ';' || character == ',' || character == ':' ||
           character == '"';
}

bool HasControl(std::string_view value) {
    for (const unsigned char character : value) {
        if (character < 0x20 || character == 0x7f) return true;
    }
    return false;
}

std::string Lowercase(std::string_view value) {
    std::string result(value);
    for (char& character : result) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

bool EqualsIgnoreCase(std::string_view left, std::string_view right) {
    return left.size() == right.size() && Lowercase(left) == Lowercase(right);
}

// Tokenizes `<name>:<value>;` fields up to the empty terminating field. Unknown single-letter
// fields are skipped but still checked for duplicates and valid escapes.
bool TokenizeFields(std::string_view body, Fields& fields) {
    std::array<bool, 26> seen{};
    size_t index = 0;
    while (true) {
        if (index >= body.size()) return false;
        if (body[index] == ';') return index + 1 == body.size();

        const char name = body[index];
        if (name < 'A' || name > 'Z' || index + 1 >= body.size() || body[index + 1] != ':') {
            return false;
        }
        if (seen[static_cast<size_t>(name - 'A')]) return false;
        seen[static_cast<size_t>(name - 'A')] = true;
        index += 2;

        std::string value;
        bool first_unescaped_quote = false;
        bool last_unescaped_quote = false;
        bool terminated = false;
        while (index < body.size()) {
            const char character = body[index];
            if (character == ';') {
                terminated = true;
                ++index;
                break;
            }
            if (character == '\\') {
                if (index + 1 >= body.size() || !IsEscapable(body[index + 1])) return false;
                value.push_back(body[index + 1]);
                last_unescaped_quote = false;
                index += 2;
                continue;
            }
            if (value.empty()) first_unescaped_quote = character == '"';
            last_unescaped_quote = character == '"';
            value.push_back(character);
            ++index;
        }
        if (!terminated) return false;

        if (Field* field = SelectField(fields, name)) {
            field->quoted = value.size() >= 2 && first_unescaped_quote && last_unescaped_quote;
            field->value = std::move(value);
        }
    }
}

bool IsHexString(std::string_view value) {
    for (const unsigned char character : value) {
        if (!std::isxdigit(character)) return false;
    }
    return true;
}

// Public DNS name only: certificate validation against the public CA bundle needs a name, so
// IP literals, single-label names and mDNS `.local` names are rejected.
bool IsPublicDomainName(std::string_view host) {
    if (host.empty() || host.size() > kMaxDomainBytes) return false;
    size_t label_start = 0;
    size_t label_count = 0;
    bool last_label_numeric = true;
    while (label_start <= host.size()) {
        size_t label_end = host.find('.', label_start);
        if (label_end == std::string_view::npos) label_end = host.size();
        const std::string_view label = host.substr(label_start, label_end - label_start);
        if (label.empty() || label.size() > kMaxLabelBytes || label.front() == '-' ||
            label.back() == '-') {
            return false;
        }
        last_label_numeric = true;
        for (const unsigned char character : label) {
            if (!(std::islower(character) || std::isdigit(character) || character == '-')) {
                return false;
            }
            if (!std::isdigit(character)) last_label_numeric = false;
        }
        ++label_count;
        label_start = label_end + 1;
    }
    return label_count >= 2 && !last_label_numeric && host.substr(host.rfind('.') + 1) != "local";
}

SetupQrError ValidateBootstrapUrl(const std::string& url, std::string& normalized) {
    if (url.empty() || url.size() > kSetupQrMaxUrlBytes) return SetupQrError::kUrlInvalid;
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return SetupQrError::kUrlInvalid;
    const std::string scheme = Lowercase(std::string_view(url).substr(0, scheme_end));
    if (scheme == "http") return SetupQrError::kUrlInsecure;
    if (url.compare(0, scheme_end, "https") != 0) return SetupQrError::kUrlInvalid;

    const size_t authority_start = scheme_end + 3;
    const size_t authority_end = url.find('/', authority_start);
    const std::string_view authority = std::string_view(url).substr(
        authority_start,
        authority_end == std::string::npos ? std::string_view::npos : authority_end - authority_start);
    if (authority.empty() || authority.front() == '[') return SetupQrError::kUrlInvalid;
    const std::string host = Lowercase(authority.substr(0, authority.find(':')));
    const std::string path =
        authority_end == std::string::npos ? std::string() : Lowercase(url.substr(authority_end));

    constexpr std::string_view kLegacyHost = "tenclass.net";
    if (host == kLegacyHost ||
        (host.size() > kLegacyHost.size() &&
         host.compare(host.size() - kLegacyHost.size() - 1, std::string::npos, ".tenclass.net") ==
             0) ||
        path.find("xiaozhi") != std::string::npos) {
        return SetupQrError::kUrlLegacy;
    }
    if (!IsValidSerialProvisioningBootstrapUrl(url) || !IsPublicDomainName(host) ||
        !NormalizeSerialProvisioningBootstrapUrl(url, normalized)) {
        normalized.clear();
        return SetupQrError::kUrlInvalid;
    }
    return SetupQrError::kNone;
}

SetupQrError ParseFields(std::string_view text, SetupQrPayload& payload) {
    const bool rodak = text.substr(0, kRodakPrefix.size()) == kRodakPrefix;
    const bool wifi = !rodak && text.substr(0, kWifiPrefix.size()) == kWifiPrefix;
    if (!rodak && !wifi) return SetupQrError::kNotProvisioning;
    if (text.size() > kSetupQrMaxPayloadBytes) return SetupQrError::kSyntaxInvalid;

    Fields fields;
    if (!TokenizeFields(text.substr(rodak ? kRodakPrefix.size() : kWifiPrefix.size()), fields)) {
        return SetupQrError::kSyntaxInvalid;
    }
    if (wifi) {
        for (Field* field : {&fields.ssid, &fields.auth, &fields.password}) {
            if (field->value && field->quoted) {
                *field->value = field->value->substr(1, field->value->size() - 2);
            }
        }
    }
    if (rodak && (!fields.version.value || *fields.version.value != "1")) {
        return SetupQrError::kVersionUnsupported;
    }

    if (!fields.ssid.value || fields.ssid.value->empty() ||
        fields.ssid.value->size() > kSetupQrMaxSsidBytes || HasControl(*fields.ssid.value)) {
        return SetupQrError::kSsidInvalid;
    }

    bool open_network = false;
    if (!fields.auth.value) {
        if (rodak) return SetupQrError::kAuthUnsupported;
        open_network = true;
    } else if (EqualsIgnoreCase(*fields.auth.value, "nopass")) {
        open_network = true;
    } else if (!EqualsIgnoreCase(*fields.auth.value, "WPA") &&
               !EqualsIgnoreCase(*fields.auth.value, "WPA2") &&
               !EqualsIgnoreCase(*fields.auth.value, "WPA3") &&
               !EqualsIgnoreCase(*fields.auth.value, "SAE")) {
        return SetupQrError::kAuthUnsupported;
    }

    const std::string password = fields.password.value.value_or(std::string());
    if (HasControl(password)) return SetupQrError::kPasswordInvalid;
    if (open_network) {
        if (!password.empty()) return SetupQrError::kPasswordInvalid;
    } else {
        const bool passphrase = password.size() >= 8 && password.size() <= 63;
        const bool raw_psk = password.size() == 64 && IsHexString(password);
        if (!passphrase && !raw_psk) return SetupQrError::kPasswordInvalid;
    }

    if (rodak) {
        if (!fields.url.value) return SetupQrError::kUrlInvalid;
        const SetupQrError url_error = ValidateBootstrapUrl(*fields.url.value, payload.bootstrap_url);
        if (url_error != SetupQrError::kNone) return url_error;
    }

    payload.kind = rodak ? SetupQrKind::kRodak : SetupQrKind::kWifiOnly;
    payload.ssid = *fields.ssid.value;
    payload.password = password;
    payload.open_network = open_network;
    return SetupQrError::kNone;
}

}  // namespace

SetupQrError ParseSetupQr(std::string_view text, SetupQrPayload& payload) {
    payload = {};
    const SetupQrError error = ParseFields(text, payload);
    if (error != SetupQrError::kNone) payload = {};
    return error;
}

const char* SetupQrErrorCode(SetupQrError error) {
    switch (error) {
        case SetupQrError::kNone: return "ok";
        case SetupQrError::kNotProvisioning: return "qr_not_provisioning";
        case SetupQrError::kVersionUnsupported: return "qr_version_unsupported";
        case SetupQrError::kSyntaxInvalid: return "qr_syntax_invalid";
        case SetupQrError::kSsidInvalid: return "qr_ssid_invalid";
        case SetupQrError::kAuthUnsupported: return "qr_auth_unsupported";
        case SetupQrError::kPasswordInvalid: return "qr_password_invalid";
        case SetupQrError::kUrlInvalid: return "qr_url_invalid";
        case SetupQrError::kUrlInsecure: return "qr_url_insecure";
        case SetupQrError::kUrlLegacy: return "qr_url_legacy";
        case SetupQrError::kServerPinned: return "qr_server_pinned";
    }
    return "qr_unknown";
}

}  // namespace rodakos
