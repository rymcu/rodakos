#include "phone_os/voice_conversation_policy.h"

#include <limits>

namespace rodakos {

void VoiceConversationPolicy::Reset() {
    completed_turns_ = 0;
    speaking_active_ = false;
    finished_ = false;
    ClearFollowUpDeadline();
}

void VoiceConversationPolicy::OnSpeakingStarted() {
    if (finished_) {
        return;
    }
    speaking_active_ = true;
    ClearFollowUpDeadline();
}

VoiceConversationAction VoiceConversationPolicy::OnSpeakingStopped(int64_t now_ms) {
    if (finished_ || !speaking_active_) {
        return VoiceConversationAction::kIgnore;
    }

    speaking_active_ = false;
    if (completed_turns_ < std::numeric_limits<uint32_t>::max()) {
        ++completed_turns_;
    }

    ArmFollowUpDeadline(now_ms);
    return VoiceConversationAction::kContinue;
}

void VoiceConversationPolicy::OnFollowUpStarted(int64_t now_ms) {
    if (finished_ || speaking_active_ || completed_turns_ == 0) {
        return;
    }
    ArmFollowUpDeadline(now_ms);
}

void VoiceConversationPolicy::OnUserSpeech(int64_t now_ms) {
    if (!finished_ && !speaking_active_ && follow_up_deadline_active_) {
        ArmFollowUpDeadline(now_ms);
    }
}

bool VoiceConversationPolicy::IsFollowUpTimedOut(int64_t now_ms) {
    // A delayed speaking-start event must never allow the old follow-up
    // deadline to terminate an interaction while TTS is still active.
    if (speaking_active_ || !follow_up_deadline_active_ ||
        now_ms < follow_up_deadline_ms_) {
        return false;
    }

    finished_ = true;
    ClearFollowUpDeadline();
    return true;
}

void VoiceConversationPolicy::ArmFollowUpDeadline(int64_t now_ms) {
    const int64_t normalized_now_ms = now_ms < 0 ? 0 : now_ms;
    follow_up_deadline_active_ = true;
    if (normalized_now_ms >
        std::numeric_limits<int64_t>::max() - follow_up_timeout_ms()) {
        follow_up_deadline_ms_ = std::numeric_limits<int64_t>::max();
    } else {
        follow_up_deadline_ms_ = normalized_now_ms + follow_up_timeout_ms();
    }
}

void VoiceConversationPolicy::ClearFollowUpDeadline() {
    follow_up_deadline_ms_ = 0;
    follow_up_deadline_active_ = false;
}

}  // namespace rodakos
