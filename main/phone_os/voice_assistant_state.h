#pragma once

#include "phone_os/cloud_diagnostic.h"
#include "phone_os/voice_turn_progress.h"

#include <cstdint>
#include <string>

namespace rodakos {

enum class VoiceAssistantPhase {
    kIdle,
    kConnecting,
    kListening,
    kSpeaking,
    kError,
};

enum class VoiceAssistantTrigger {
    kManual,
    kWakeWord,
    kRemote,
};

struct VoiceAssistantState {
    VoiceAssistantPhase phase = VoiceAssistantPhase::kIdle;
    VoiceAssistantTrigger trigger = VoiceAssistantTrigger::kManual;
    bool initialized = false;
    bool stopping = false;
    bool focus_active = false;
    bool transport_active = false;
    bool recorder_active = false;
    // 服务端发起的只放音播报（音乐推送等）：不开麦，靠唤醒监听打断
    bool announce = false;
    // turn.accepted 之后、output.start 之前（契约 §6.1）。服务端已接下这一轮，
    // 由 VoiceConversationPolicy 的 130 s 时限负责结束丢失的轮次。
    bool awaiting_reply = false;
    // 仅在 awaiting_reply 期间有值：最近一条 turn.progress 的步骤和服务端给的显示文字。
    VoiceTurnStage turn_stage = VoiceTurnStage::kNone;
    std::string turn_label;
    uint32_t focus_token = 0;
    std::string message;
    std::string last_wake_word;
    std::string transport_name;
    std::string recorder_name;
    CloudDiagnosticCode diagnostic = CloudDiagnosticCode::kReady;
    int64_t diagnostic_at_ms = 0;
    uint32_t diagnostic_revision = 0;
};

// What the status light needs, without the strings of VoiceAssistantState: it is read about
// 30 times a second.
struct VoiceStatusLightSnapshot {
    VoiceAssistantPhase phase = VoiceAssistantPhase::kIdle;
    bool reconnecting = false;
    // Between turn.accepted and output.start (contract section 6.1).
    bool awaiting_reply = false;
    bool user_speech = false;
    // Level of the reply frame that is playing now, 0..255. 0 when nothing plays.
    uint8_t playback_level = 0;
    // Time left in the follow-up window; negative when no window runs.
    int64_t follow_up_remaining_ms = -1;
    // Counters only increase; the light reacts when one changes.
    uint32_t interaction_starts = 0;
    uint32_t barge_ins = 0;
};

}  // namespace rodakos
