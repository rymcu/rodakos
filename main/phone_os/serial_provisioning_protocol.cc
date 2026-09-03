#include "phone_os/serial_provisioning_protocol.h"

#include <cctype>
#include <string_view>

namespace rodakos {
namespace {

bool HasControlOrWhitespace(const std::string& value) {
    for (const unsigned char character : value) {
        if (character <= 0x20 || character == 0x7f) {
            return true;
        }
    }
    return false;
}

bool IsValidPort(std::string_view port) {
    if (port.empty()) {
        return false;
    }
    unsigned value = 0;
    for (const unsigned char character : port) {
        if (!std::isdigit(character)) {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(character - '0');
        if (value > 65535) {
            return false;
        }
    }
    return value > 0;
}

bool IsValidAuthority(std::string_view authority) {
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        authority.find('\\') != std::string_view::npos) {
        return false;
    }

    if (authority.front() == '[') {
        const size_t closing_bracket = authority.find(']');
        if (closing_bracket == std::string_view::npos || closing_bracket <= 1) {
            return false;
        }
        const std::string_view suffix = authority.substr(closing_bracket + 1);
        return suffix.empty() || (suffix.front() == ':' && IsValidPort(suffix.substr(1)));
    }

    if (authority.find('[') != std::string_view::npos ||
        authority.find(']') != std::string_view::npos) {
        return false;
    }
    const size_t first_colon = authority.find(':');
    const size_t last_colon = authority.rfind(':');
    if (first_colon != last_colon) {
        return false;
    }
    const std::string_view host = authority.substr(0, first_colon);
    if (host.empty()) {
        return false;
    }
    return first_colon == std::string_view::npos ||
           IsValidPort(authority.substr(first_colon + 1));
}

}  // namespace

SerialProvisioningFrameAccumulator::SerialProvisioningFrameAccumulator() {
    line_.reserve(kSerialProvisioningMaxFrameBytes - 1);
}

SerialProvisioningFrameResult SerialProvisioningFrameAccumulator::Push(
    uint8_t byte, std::string& line) {
    if (byte == '\n') {
        const bool too_large = oversized_ ||
                               pending_wire_bytes_ + 1 > kSerialProvisioningMaxFrameBytes;
        if (too_large) {
            line.clear();
            Reset();
            return SerialProvisioningFrameResult::kFrameTooLarge;
        }

        line = line_;
        // Treat only the CR immediately before LF as a line-ending marker.
        // Any embedded CR remains visible to the JSON validator as a control
        // character instead of being silently removed from a credential.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        Reset();
        return SerialProvisioningFrameResult::kLineReady;
    }

    if (oversized_) {
        return SerialProvisioningFrameResult::kNeedMore;
    }

    // Reserve one byte for the terminating LF. This keeps the accumulator
    // bounded even when a sender never terminates an oversized line.
    if (pending_wire_bytes_ >= kSerialProvisioningMaxFrameBytes - 1) {
        oversized_ = true;
        return SerialProvisioningFrameResult::kNeedMore;
    }

    ++pending_wire_bytes_;
    line_.push_back(static_cast<char>(byte));
    return SerialProvisioningFrameResult::kNeedMore;
}

void SerialProvisioningFrameAccumulator::Reset() {
    line_.clear();
    pending_wire_bytes_ = 0;
    oversized_ = false;
}

bool IsValidSerialProvisioningBootstrapUrl(const std::string& url) {
    if (url.empty() || url.size() > kSerialProvisioningMaxBootstrapUrlBytes ||
        HasControlOrWhitespace(url)) {
        return false;
    }
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos ||
        (url.compare(0, scheme_end, "http") != 0 &&
         url.compare(0, scheme_end, "https") != 0)) {
        return false;
    }
    const size_t authority_start = scheme_end + 3;
    const size_t authority_end = url.find_first_of("/?#", authority_start);
    const size_t authority_size = authority_end == std::string::npos
                                      ? std::string_view::npos
                                      : authority_end - authority_start;
    return IsValidAuthority(
        std::string_view(url).substr(authority_start, authority_size));
}

bool ContainsSerialProvisioningJsonNul(const std::string& json) {
    for (size_t index = 0; index < json.size();) {
        if (json[index] == '\0') {
            return true;
        }
        if (json[index] == '\\') {
            if (index + 5 < json.size() && json[index + 1] == 'u' &&
                json[index + 2] == '0' && json[index + 3] == '0' &&
                json[index + 4] == '0' && json[index + 5] == '0') {
                return true;
            }
            index += index + 1 < json.size() ? 2 : 1;
            continue;
        }
        ++index;
    }
    return false;
}

}  // namespace rodakos
