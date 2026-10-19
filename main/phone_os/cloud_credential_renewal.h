#pragma once

#include <algorithm>
#include <cstdint>

namespace rodakos {

// 访问令牌的后台续期时机。语音唤醒前只给凭据准备 3 s；慢网络上两次全新 HTTPS
// 握手放不进去，令牌一到期唤醒就失败。后台在到期前续期，唤醒时令牌总是新的。
class CloudCredentialRenewal {
public:
    // 到期前多久开始续期：寿命的 1/4，夹在 [60 s, 300 s]，且不早于寿命的一半。
    static int64_t LeadMs(int64_t lifetime_ms) {
        const int64_t lead = std::clamp<int64_t>(lifetime_ms / 4, 60000, 300000);
        return std::min(lead, lifetime_ms / 2);
    }

    void ObserveToken(int64_t now_ms, int64_t expires_at_ms) {
        failures_ = 0;
        retry_at_ms_ = 0;
        due_at_ms_ = 0;
        if (expires_at_ms <= now_ms) return;
        due_at_ms_ = expires_at_ms - LeadMs(expires_at_ms - now_ms);
    }

    void ObserveFailure(int64_t now_ms) {
        failures_ = std::min(failures_ + 1, 6);
        retry_at_ms_ = now_ms + (static_cast<int64_t>(15000) << (failures_ - 1));
    }

    void Clear() {
        failures_ = 0;
        retry_at_ms_ = 0;
        due_at_ms_ = 0;
    }

    bool Due(int64_t now_ms) const {
        return due_at_ms_ != 0 && now_ms >= due_at_ms_ && now_ms >= retry_at_ms_;
    }

    int64_t due_at_ms() const { return due_at_ms_; }
    int64_t retry_at_ms() const { return retry_at_ms_; }

private:
    int64_t due_at_ms_ = 0;
    int64_t retry_at_ms_ = 0;
    int failures_ = 0;
};

}  // namespace rodakos
