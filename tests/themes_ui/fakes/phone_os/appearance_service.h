#pragma once

// Replaces the production service, which needs NVS, SD, HTTP and signing. Only the calls
// made by ThemesApp and theme_presets are provided.
#include "phone_os/appearance_status.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rodakos {

class AppearanceService {
public:
    AppearanceStatusSnapshot GetStatusSnapshot() const {
        ++snapshot_calls;
        return snapshot;
    }

    bool SetLocalTheme(const std::string& preset, uint32_t primary) {
        ++set_calls;
        if (!save_ok) return false;
        local_preset = preset;
        local_primary = primary;
        snapshot.theme_local = true;
        return true;
    }

    void RequestPublisher() { ++publisher_requests; }
    AppearancePublisherState GetPublisherState() const { return publisher; }
    bool ConfirmPublisher(const std::string& key_id) {
        confirmed_keys.push_back(key_id);
        if (!confirm_ok || key_id != publisher.key_id) return false;
        publisher.trusted = true;
        snapshot.publisher_trusted = true;
        return true;
    }

    AppearanceStatusSnapshot snapshot;
    AppearancePublisherState publisher;
    bool save_ok = true;
    bool confirm_ok = true;
    int set_calls = 0;
    int publisher_requests = 0;
    mutable int snapshot_calls = 0;
    std::string local_preset;
    uint32_t local_primary = 0;
    std::vector<std::string> confirmed_keys;
};

}  // namespace rodakos
