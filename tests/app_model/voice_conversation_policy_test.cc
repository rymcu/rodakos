#include "test_framework.h"

#include "phone_os/voice_conversation_policy.h"

#include <cstdint>
#include <limits>

namespace {

using rodakos::VoiceConversationAction;
using rodakos::VoiceConversationPolicy;

}  // namespace

RODAK_TEST("voice conversation continues beyond five replies without a fixed turn limit") {
    VoiceConversationPolicy policy;
    int64_t now_ms = 1000;

    for (uint32_t turn = 1; turn <= 8; ++turn) {
        policy.OnSpeakingStarted();
        RODAK_CHECK_EQ(policy.OnSpeakingStopped(now_ms), VoiceConversationAction::kContinue);
        RODAK_CHECK_EQ(policy.completed_turns(), turn);
        RODAK_CHECK(policy.waiting_for_follow_up());
        RODAK_CHECK_EQ(policy.follow_up_deadline_ms(),
                       now_ms + policy.follow_up_timeout_ms());
        now_ms += 1000;
    }
}

RODAK_TEST("voice conversation follow-up timeout is inclusive and idempotent") {
    VoiceConversationPolicy policy;
    constexpr int64_t stopped_at_ms = 42;

    policy.OnSpeakingStarted();
    RODAK_CHECK_EQ(policy.OnSpeakingStopped(stopped_at_ms),
                   VoiceConversationAction::kContinue);
    const int64_t deadline_ms = stopped_at_ms + policy.follow_up_timeout_ms();
    RODAK_CHECK_FALSE(policy.IsFollowUpTimedOut(deadline_ms - 1));
    RODAK_CHECK(policy.IsFollowUpTimedOut(deadline_ms));
    RODAK_CHECK_FALSE(policy.IsFollowUpTimedOut(deadline_ms + 1));
    RODAK_CHECK_FALSE(policy.waiting_for_follow_up());
}

RODAK_TEST("voice conversation restarts the follow-up deadline when listening begins") {
    VoiceConversationPolicy policy;

    policy.OnSpeakingStarted();
    RODAK_CHECK_EQ(policy.OnSpeakingStopped(100), VoiceConversationAction::kContinue);
    policy.OnFollowUpStarted(500);

    RODAK_CHECK_EQ(policy.follow_up_deadline_ms(), 500 + policy.follow_up_timeout_ms());
    RODAK_CHECK_FALSE(policy.IsFollowUpTimedOut(100 + policy.follow_up_timeout_ms()));
    RODAK_CHECK(policy.IsFollowUpTimedOut(500 + policy.follow_up_timeout_ms()));
}

RODAK_TEST("voice conversation clears the follow-up deadline when speaking starts") {
    VoiceConversationPolicy policy;

    policy.OnSpeakingStarted();
    RODAK_CHECK_EQ(policy.OnSpeakingStopped(100), VoiceConversationAction::kContinue);
    policy.OnSpeakingStarted();

    RODAK_CHECK_FALSE(policy.waiting_for_follow_up());
    RODAK_CHECK_FALSE(policy.IsFollowUpTimedOut(100 + policy.follow_up_timeout_ms()));
}

RODAK_TEST("voice conversation timeout is suppressed while speaking is active") {
    VoiceConversationPolicy policy;

    policy.OnSpeakingStarted();
    RODAK_CHECK_EQ(policy.OnSpeakingStopped(100), VoiceConversationAction::kContinue);
    const int64_t deadline_ms = policy.follow_up_deadline_ms();

    // Model a delayed/queued speaking-start event arriving at the deadline.
    policy.OnSpeakingStarted();
    RODAK_CHECK_FALSE(policy.IsFollowUpTimedOut(deadline_ms));
    RODAK_CHECK_FALSE(policy.waiting_for_follow_up());
}

RODAK_TEST("voice conversation ignores duplicate speaking stop events") {
    VoiceConversationPolicy policy;

    policy.OnSpeakingStarted();
    RODAK_CHECK_EQ(policy.OnSpeakingStopped(100), VoiceConversationAction::kContinue);
    const int64_t deadline_ms = policy.follow_up_deadline_ms();
    RODAK_CHECK_EQ(policy.OnSpeakingStopped(200), VoiceConversationAction::kIgnore);
    RODAK_CHECK_EQ(policy.completed_turns(), uint32_t{1});
    RODAK_CHECK_EQ(policy.follow_up_deadline_ms(), deadline_ms);
}

RODAK_TEST("voice conversation reset clears turns deadline and speaking state") {
    VoiceConversationPolicy policy;

    policy.OnSpeakingStarted();
    RODAK_CHECK_EQ(policy.OnSpeakingStopped(100), VoiceConversationAction::kContinue);
    policy.OnSpeakingStarted();
    policy.Reset();

    RODAK_CHECK_EQ(policy.completed_turns(), uint32_t{0});
    RODAK_CHECK_FALSE(policy.waiting_for_follow_up());
    RODAK_CHECK_EQ(policy.follow_up_deadline_ms(), int64_t{0});
    RODAK_CHECK_EQ(policy.OnSpeakingStopped(200), VoiceConversationAction::kIgnore);
    RODAK_CHECK_FALSE(policy.IsFollowUpTimedOut(200));
}

RODAK_TEST("voice conversation follow-up deadline saturates at the clock limit") {
    VoiceConversationPolicy policy;
    constexpr int64_t max_ms = std::numeric_limits<int64_t>::max();

    policy.OnSpeakingStarted();
    RODAK_CHECK_EQ(policy.OnSpeakingStopped(max_ms - 10),
                   VoiceConversationAction::kContinue);
    RODAK_CHECK_EQ(policy.follow_up_deadline_ms(), max_ms);
    RODAK_CHECK_FALSE(policy.IsFollowUpTimedOut(max_ms - 1));
    RODAK_CHECK(policy.IsFollowUpTimedOut(max_ms));
}

RODAK_TEST("voice conversation ignores follow-up starts outside an active conversation") {
    VoiceConversationPolicy policy;

    policy.OnFollowUpStarted(100);

    RODAK_CHECK_FALSE(policy.waiting_for_follow_up());
    RODAK_CHECK_FALSE(policy.IsFollowUpTimedOut(100 + policy.follow_up_timeout_ms()));
}
