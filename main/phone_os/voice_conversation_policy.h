#pragma once

#include <cstdint>

namespace rodakos {

enum class VoiceConversationAction {
    kContinue,
    kIgnore,
};

class VoiceConversationPolicy {
public:
    static constexpr int64_t follow_up_timeout_ms() { return 30 * 1000; }

    void Reset();
    void OnSpeakingStarted();
    VoiceConversationAction OnSpeakingStopped(int64_t now_ms);
    void OnFollowUpStarted(int64_t now_ms);
    void OnUserSpeech(int64_t now_ms);
    bool IsFollowUpTimedOut(int64_t now_ms);

    uint32_t completed_turns() const { return completed_turns_; }
    bool waiting_for_follow_up() const { return follow_up_deadline_active_; }
    int64_t follow_up_deadline_ms() const { return follow_up_deadline_ms_; }

private:
    void ArmFollowUpDeadline(int64_t now_ms);
    void ClearFollowUpDeadline();

    uint32_t completed_turns_ = 0;
    int64_t follow_up_deadline_ms_ = 0;
    bool follow_up_deadline_active_ = false;
    bool speaking_active_ = false;
    bool finished_ = false;
};

}  // namespace rodakos
