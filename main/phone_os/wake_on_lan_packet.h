#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace rodakos {

constexpr size_t kWakeOnLanMacBytes = 6;
constexpr size_t kWakeOnLanPacketBytes = 102;

bool ParseWakeOnLanMac(std::string_view text,
                       std::array<uint8_t, kWakeOnLanMacBytes>* mac);
std::string FormatWakeOnLanMac(const std::array<uint8_t, kWakeOnLanMacBytes>& mac);
bool NormalizeWakeOnLanIpv4(std::string_view text, std::string* normalized);
std::array<uint8_t, kWakeOnLanPacketBytes> BuildWakeOnLanPacket(
    const std::array<uint8_t, kWakeOnLanMacBytes>& mac);

}  // namespace rodakos
