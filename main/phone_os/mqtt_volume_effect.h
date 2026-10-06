#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rodakos {

class AudioOutputService;

// The caller guards the authenticated MQTT epoch through the whole Handle call.
class MqttVolumeEffect {
public:
    explicit MqttVolumeEffect(AudioOutputService* output);
    void ResetAuthority();
    std::string Handle(const std::string& payload, const std::string& device_key,
                       bool& volume_handled);

private:
    struct Entry {
        std::string effect_id;
        std::string request;
        std::string response;
    };
    AudioOutputService* output_;
    std::mutex mutex_;
    uint64_t volume_shadow_version_ = 0;
    std::vector<Entry> ledger_;
};

}  // namespace rodakos
