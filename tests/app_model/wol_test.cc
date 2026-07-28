#include "test_framework.h"

#include "apps/wol/wol_device_codec.h"
#include "phone_os/wake_on_lan_packet.h"

#include <array>
#include <string>
#include <vector>

RODAK_TEST("Wake-on-LAN MAC parsing normalizes unicast addresses") {
    std::array<uint8_t, rodakos::kWakeOnLanMacBytes> mac{};
    RODAK_CHECK(rodakos::ParseWakeOnLanMac("a0-b1-c2-d3-e4-f5", &mac));
    RODAK_CHECK_EQ(rodakos::FormatWakeOnLanMac(mac), "A0:B1:C2:D3:E4:F5");
    RODAK_CHECK_FALSE(rodakos::ParseWakeOnLanMac("00:00:00:00:00:00", &mac));
    RODAK_CHECK_FALSE(rodakos::ParseWakeOnLanMac("FF:FF:FF:FF:FF:FF", &mac));
    RODAK_CHECK_FALSE(rodakos::ParseWakeOnLanMac("01:11:22:33:44:55", &mac));
    RODAK_CHECK_FALSE(rodakos::ParseWakeOnLanMac("AA:BB:CC:DD:EE", &mac));
}

RODAK_TEST("Wake-on-LAN magic packet contains the sync stream and sixteen MAC copies") {
    const std::array<uint8_t, rodakos::kWakeOnLanMacBytes> mac = {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5};
    const auto packet = rodakos::BuildWakeOnLanPacket(mac);
    RODAK_CHECK_EQ(packet.size(), size_t{102});
    for (size_t index = 0; index < 6; ++index) {
        RODAK_CHECK_EQ(packet[index], uint8_t{0xff});
    }
    for (size_t repetition = 0; repetition < 16; ++repetition) {
        for (size_t index = 0; index < mac.size(); ++index) {
            RODAK_CHECK_EQ(packet[6 + repetition * mac.size() + index], mac[index]);
        }
    }
}

RODAK_TEST("Wake-on-LAN IPv4 normalization rejects malformed addresses") {
    std::string address;
    RODAK_CHECK(rodakos::NormalizeWakeOnLanIpv4("192.168.001.255", &address));
    RODAK_CHECK_EQ(address, "192.168.1.255");
    RODAK_CHECK_FALSE(rodakos::NormalizeWakeOnLanIpv4("192.168.1", &address));
    RODAK_CHECK_FALSE(rodakos::NormalizeWakeOnLanIpv4("192.168.1.256", &address));
    RODAK_CHECK_FALSE(rodakos::NormalizeWakeOnLanIpv4("192.168.1.1.2", &address));
}

RODAK_TEST("Wake-on-LAN device codec round-trips normalized versioned data") {
    rodakos::WolDevice device;
    device.name = "Office PC";
    device.mac_address = "a0-b1-c2-d3-e4-f5";
    device.broadcast_address = "192.168.001.255";
    device.port = 9;
    std::vector<rodakos::WolDevice> devices = {device};
    const rodakos::WolDeviceEncodeResult encoded = rodakos::EncodeWolDevices(devices);
    RODAK_CHECK_EQ(encoded.status, rodakos::WolDeviceCodecStatus::kOk);
    const rodakos::WolDeviceDecodeResult decoded = rodakos::DecodeWolDevices(encoded.json);
    RODAK_CHECK_EQ(decoded.status, rodakos::WolDeviceCodecStatus::kOk);
    RODAK_CHECK_EQ(decoded.devices.size(), size_t{1});
    RODAK_CHECK_EQ(decoded.devices[0].name, "Office PC");
    RODAK_CHECK_EQ(decoded.devices[0].mac_address, "A0:B1:C2:D3:E4:F5");
    RODAK_CHECK_EQ(decoded.devices[0].broadcast_address, "192.168.1.255");
    RODAK_CHECK_EQ(decoded.devices[0].port, uint16_t{9});
}

RODAK_TEST("Wake-on-LAN device codec preserves unsupported or invalid documents") {
    RODAK_CHECK_EQ(
        rodakos::DecodeWolDevices("{\"version\":2,\"devices\":[]}").status,
        rodakos::WolDeviceCodecStatus::kUnsupportedVersion);
    RODAK_CHECK_EQ(
        rodakos::DecodeWolDevices(
            "{\"version\":1,\"devices\":[{\"name\":\"PC\",\"mac\":\"bad\","
            "\"broadcast\":\"255.255.255.255\",\"port\":9}]}").status,
        rodakos::WolDeviceCodecStatus::kInvalidDevice);

    std::vector<rodakos::WolDevice> too_many(rodakos::kWolMaxDevices + 1);
    RODAK_CHECK_EQ(rodakos::EncodeWolDevices(too_many).status,
                   rodakos::WolDeviceCodecStatus::kInvalidDevice);
}
