#include "phone_os/wake_on_lan_service.h"

#include "phone_os/wake_on_lan_packet.h"
#include "rodakos_adapters/wifi_adapter.h"

#include <esp_log.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <array>
#include <cerrno>
#include <string>

namespace rodakos {
namespace {
constexpr const char* TAG = "WakeOnLan";
}

WakeOnLanResult WakeOnLanService::Send(std::string_view mac_address,
                                       std::string_view broadcast_address,
                                       uint16_t port) const {
    if (wifi_ == nullptr || wifi_->GetStatus() != WiFiStatus::kConnected) {
        return WakeOnLanResult::kWifiDisconnected;
    }

    std::array<uint8_t, kWakeOnLanMacBytes> mac{};
    if (!ParseWakeOnLanMac(mac_address, &mac)) {
        return WakeOnLanResult::kInvalidMac;
    }

    std::string normalized_address;
    if (!NormalizeWakeOnLanIpv4(broadcast_address, &normalized_address)) {
        return WakeOnLanResult::kInvalidBroadcastAddress;
    }
    if (port == 0) {
        return WakeOnLanResult::kInvalidPort;
    }

    const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "UDP socket creation failed: errno=%d", errno);
        return WakeOnLanResult::kSocketOpenFailed;
    }

    const int enabled = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) != 0) {
        ESP_LOGE(TAG, "Enabling UDP broadcast failed: errno=%d", errno);
        close(socket_fd);
        return WakeOnLanResult::kSocketConfigureFailed;
    }

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    if (inet_pton(AF_INET, normalized_address.c_str(), &destination.sin_addr) != 1) {
        close(socket_fd);
        return WakeOnLanResult::kInvalidBroadcastAddress;
    }

    const auto packet = BuildWakeOnLanPacket(mac);
    const int sent = sendto(socket_fd, packet.data(), packet.size(), 0,
                            reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    close(socket_fd);
    if (sent != static_cast<int>(packet.size())) {
        ESP_LOGE(TAG, "Magic packet send failed: sent=%d errno=%d", sent, errno);
        return WakeOnLanResult::kSendFailed;
    }

    ESP_LOGI(TAG, "Magic packet sent to %s:%u", normalized_address.c_str(),
             static_cast<unsigned>(port));
    return WakeOnLanResult::kSent;
}

}  // namespace rodakos
