#pragma once

#include <cstdint>

#include "esp_peer_default.h"

namespace rodakos {

constexpr uint8_t kWebRtcPeerMaxCandidates = 32;

inline esp_peer_default_cfg_t MakeWebRtcPeerDefaultConfig() {
    esp_peer_default_cfg_t config{};
    // The pinned S3 library uses 10 for zero, despite the header's default-16
    // comment. Its public field controls both local and remote candidate tables.
    config.max_candidates = kWebRtcPeerMaxCandidates;
    // Keep the API lock responsive while the JPEG sender waits for SCTP space.
    config.agent_recv_timeout = 50;
    config.data_ch_cfg.send_cache_size = 400 * 1024;
    config.data_ch_cfg.recv_cache_size = 400 * 1024;
    return config;
}

}  // namespace rodakos
