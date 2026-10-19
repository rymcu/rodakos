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
    // output.start 之后多久没有新的回复音频就结束会话。按帧间隔计，不按回复总长：
    // 正常回复逐帧到达，最长的一段也远短于此；服务器自身的轮次超时是 120 s。
    static constexpr int64_t speaking_stall_timeout_ms() { return 20 * 1000; }

    void Reset();
    void OnSpeakingStarted();
    // 有 now_ms 的重载同时刷新回复进度；无参版本只保留旧语义，供不带时钟的路径使用。
    void OnSpeakingStarted(int64_t now_ms);
    void OnSpeakingAudio(int64_t now_ms);
    VoiceConversationAction OnSpeakingStopped(int64_t now_ms);
    void OnFollowUpStarted(int64_t now_ms);
    void OnUserSpeech(int64_t now_ms);
    bool IsFollowUpTimedOut(int64_t now_ms);
    // 回复开始后一直没有音频或 output.stop，即服务器停住；只触发一次。
    bool IsSpeakingStalled(int64_t now_ms);

    uint32_t completed_turns() const { return completed_turns_; }
    bool waiting_for_follow_up() const { return follow_up_deadline_active_; }
    int64_t follow_up_deadline_ms() const { return follow_up_deadline_ms_; }
    bool waiting_for_speaking_progress() const { return speaking_deadline_active_; }
    int64_t speaking_deadline_ms() const { return speaking_deadline_ms_; }

private:
    void ArmFollowUpDeadline(int64_t now_ms);
    void ClearFollowUpDeadline();
    void ArmSpeakingDeadline(int64_t now_ms);
    void ClearSpeakingDeadline();

    uint32_t completed_turns_ = 0;
    int64_t follow_up_deadline_ms_ = 0;
    bool follow_up_deadline_active_ = false;
    int64_t speaking_deadline_ms_ = 0;
    bool speaking_deadline_active_ = false;
    bool speaking_active_ = false;
    bool finished_ = false;
};

}  // namespace rodakos
