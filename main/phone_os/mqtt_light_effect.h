#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rodakos {
class LightService;

// The caller holds its authenticated MQTT epoch lock through Handle, including the driver write.
class MqttLightEffect {
public:
    explicit MqttLightEffect(LightService* lights);
    void ResetAuthority();
    std::string Handle(const std::string& payload, const std::string& device_key);
private:
    struct Entry { std::string effect_id; std::string request; std::string response; };
    struct Watermark { std::string light_id; uint64_t version = 0; };
    LightService* lights_;
    std::mutex mutex_;
    std::vector<Entry> ledger_;
    std::vector<Watermark> watermarks_;
    uint64_t authority_version_ = 0;
    // The MQTT worker has a 6 KiB stack; do not put this beside cJSON/parser frames.
    char response_buffer_[2048] = {};
};
}
