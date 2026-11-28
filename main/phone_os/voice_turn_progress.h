#pragma once

#include <cstdint>
#include <string>

namespace rodakos {

// Step of an accepted turn from turn.progress (contract section 6.1). kNone until the first
// progress event of the turn.
enum class VoiceTurnStage {
    kNone,
    kThinking,
    kTool,
    kComposing,
};

// What the Shell voice overlay needs, read a few times a second when the revision changes.
struct VoiceTurnProgressSnapshot {
    // Between turn.accepted and output.start.
    bool awaiting_reply = false;
    VoiceTurnStage stage = VoiceTurnStage::kNone;
    std::string label;
    // The reply sentence the speaker is playing now (output.caption, or the output.start text of a
    // session without captions). Empty outside playback and right after an interrupt.
    std::string caption;
    // Increases whenever a field above may have changed.
    uint32_t revision = 0;
};

// Lets the Shell read the voice display state without depending on the assistant service, whose
// test fakes define their own state types.
class VoiceTurnProgressProvider {
public:
    virtual ~VoiceTurnProgressProvider() = default;
    virtual VoiceTurnProgressSnapshot GetTurnProgressSnapshot() = 0;
    // Cheap check before a snapshot: no string copies.
    virtual uint32_t GetVoiceDisplayRevision() = 0;
};

}  // namespace rodakos
