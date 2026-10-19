#include "phone_os/voice_conversation_policy.h"

#include <limits>

namespace rodakos {

void VoiceConversationPolicy::Reset() {
    completed_turns_ = 0;
    speaking_active_ = false;
    finished_ = false;
    ClearFollowUpDeadline();
    ClearSpeakingDeadline();
}

void VoiceConversationPolicy::OnSpeakingStarted() {
    if (finished_) {
        return;
    }
    speaking_active_ = true;
    ClearFollowUpDeadline();
}

void VoiceConversationPolicy::OnSpeakingStarted(int64_t now_ms) {
    if (finished_) {
        return;
    }
    OnSpeakingStarted();
    ArmSpeakingDeadline(now_ms);
}

void VoiceConversationPolicy::OnSpeakingAudio(int64_t now_ms) {
    if (finished_ || !speaking_active_) {
        return;
    }
    ArmSpeakingDeadline(now_ms);
}

VoiceConversationAction VoiceConversationPolicy::OnSpeakingStopped(int64_t now_ms) {
    if (finished_ || !speaking_active_) {
        return VoiceConversationAction::kIgnore;
    }

    speaking_active_ = false;
    ClearSpeakingDeadline();
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

bool VoiceConversationPolicy::IsSpeakingStalled(int64_t now_ms) {
    if (finished_ || !speaking_active_ || !speaking_deadline_active_ ||
        now_ms < speaking_deadline_ms_) {
        return false;
    }

    finished_ = true;
    ClearSpeakingDeadline();
    return true;
}

void VoiceConversationPolicy::ArmSpeakingDeadline(int64_t now_ms) {
    const int64_t normalized_now_ms = now_ms < 0 ? 0 : now_ms;
    speaking_deadline_active_ = true;
    if (normalized_now_ms >
        std::numeric_limits<int64_t>::max() - speaking_stall_timeout_ms()) {
        speaking_deadline_ms_ = std::numeric_limits<int64_t>::max();
    } else {
        speaking_deadline_ms_ = normalized_now_ms + speaking_stall_timeout_ms();
    }
}

void VoiceConversationPolicy::ClearSpeakingDeadline() {
    speaking_deadline_ms_ = 0;
    speaking_deadline_active_ = false;
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
