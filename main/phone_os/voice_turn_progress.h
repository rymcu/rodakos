#pragma once

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

// What the Home screen needs to show an accepted turn, read a few times a second.
struct VoiceTurnProgressSnapshot {
    bool awaiting_reply = false;
    VoiceTurnStage stage = VoiceTurnStage::kNone;
    std::string label;
};

// Lets the Home screen read the turn progress without depending on the assistant service, whose
// test fakes define their own state types.
class VoiceTurnProgressProvider {
public:
    virtual ~VoiceTurnProgressProvider() = default;
    virtual VoiceTurnProgressSnapshot GetTurnProgressSnapshot() = 0;
};

}  // namespace rodakos
