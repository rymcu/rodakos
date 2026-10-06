#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace rodakos {

class AudioOutputService;

// The canonical transport permits one immutable session per connection generation.
class VoiceVolumeMcp {
public:
    explicit VoiceVolumeMcp(AudioOutputService& output);
    bool Bind(uint32_t transport_generation);
    void Stop();
    std::string Handle(const std::string& payload, uint32_t transport_generation,
                       const std::function<bool()>& can_execute = {});

private:
    struct Entry {
        std::string id;
        std::string request;
        std::string effect_id;
        std::string response;
    };
    AudioOutputService& output_;
    std::mutex mutex_;
    uint32_t generation_ = 0;
    uint32_t last_generation_ = 0;
    bool initialized_ = false;
    std::vector<Entry> ledger_;
};

}  // namespace rodakos
