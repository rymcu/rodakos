#include "phone_os/wake_on_lan_packet.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace rodakos {
namespace {

int HexValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
}

bool ParseIpv4Octet(std::string_view text, uint8_t* octet) {
    if (octet == nullptr || text.empty() || text.size() > 3) {
        return false;
    }
    unsigned value = 0;
    for (char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(character - '0');
    }
    if (value > 255) {
        return false;
    }
    *octet = static_cast<uint8_t>(value);
    return true;
}

}  // namespace

bool ParseWakeOnLanMac(std::string_view text,
                       std::array<uint8_t, kWakeOnLanMacBytes>* mac) {
    if (mac == nullptr || text.size() != 17) {
        return false;
    }

    std::array<uint8_t, kWakeOnLanMacBytes> parsed{};
    const char separator = text[2];
    if (separator != ':' && separator != '-') {
        return false;
    }
    for (size_t index = 0; index < parsed.size(); ++index) {
        const size_t offset = index * 3;
        const int high = HexValue(text[offset]);
        const int low = HexValue(text[offset + 1]);
        if (high < 0 || low < 0 || (index + 1 < parsed.size() && text[offset + 2] != separator)) {
            return false;
        }
        parsed[index] = static_cast<uint8_t>((high << 4) | low);
    }

    const bool all_zero = std::all_of(parsed.begin(), parsed.end(), [](uint8_t byte) {
        return byte == 0;
    });
    const bool all_broadcast = std::all_of(parsed.begin(), parsed.end(), [](uint8_t byte) {
        return byte == 0xff;
    });
    if (all_zero || all_broadcast || (parsed[0] & 0x01U) != 0) {
        return false;
    }

    *mac = parsed;
    return true;
}

std::string FormatWakeOnLanMac(const std::array<uint8_t, kWakeOnLanMacBytes>& mac) {
    char text[18] = {};
    std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return text;
}

bool NormalizeWakeOnLanIpv4(std::string_view text, std::string* normalized) {
    if (normalized == nullptr || text.empty() || text.size() > 15) {
        return false;
    }

    std::array<uint8_t, 4> octets{};
    size_t start = 0;
    for (size_t index = 0; index < octets.size(); ++index) {
        const size_t end = index + 1 == octets.size() ? text.size() : text.find('.', start);
        if (end == std::string_view::npos || !ParseIpv4Octet(text.substr(start, end - start), &octets[index])) {
            return false;
        }
        start = end + 1;
    }
    if (start != text.size() + 1) {
        return false;
    }

    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
                  static_cast<unsigned>(octets[0]), static_cast<unsigned>(octets[1]),
                  static_cast<unsigned>(octets[2]), static_cast<unsigned>(octets[3]));
    *normalized = buffer;
    return true;
}

std::array<uint8_t, kWakeOnLanPacketBytes> BuildWakeOnLanPacket(
    const std::array<uint8_t, kWakeOnLanMacBytes>& mac) {
    std::array<uint8_t, kWakeOnLanPacketBytes> packet{};
    std::fill_n(packet.begin(), 6, 0xff);
    for (size_t repetition = 0; repetition < 16; ++repetition) {
        std::copy(mac.begin(), mac.end(), packet.begin() + 6 + repetition * mac.size());
    }
    return packet;
}

}  // namespace rodakos
