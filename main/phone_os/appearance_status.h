#pragma once

#include <cstdint>
#include <string>

namespace rodakos {

struct AppearancePublisherState {
    std::string key_id;
    std::string fingerprint;
    std::string public_key;
    bool trusted = false;
    bool loading = false;
    std::string error;
};

// UI copy of AppearanceService state. The reported shadow JSON stays the cloud contract.
struct AppearanceStatusSnapshot {
    std::string status;
    int progress = 0;
    std::string error;
    std::string fallback_reason;
    uint32_t active_revision = 0;
    uint32_t pending_revision = 0;
    bool theme_local = false;
    bool publisher_trusted = false;
};

}  // namespace rodakos
