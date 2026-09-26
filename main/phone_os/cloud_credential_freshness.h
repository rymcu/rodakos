#pragma once

#include <algorithm>
#include <cstdint>

namespace rodakos {

class CloudCredentialFreshness {
public:
    void ObserveRefresh(bool succeeded, int64_t started_ms, int lifetime_seconds) {
        refresh_after_ms_ = 0;
        expires_at_ms_ = 0;
        if (!succeeded || started_ms < 0 || lifetime_seconds <= 0) {
            return;
        }
        const int margin_seconds = std::min(30, lifetime_seconds / 10);
        expires_at_ms_ = started_ms + static_cast<int64_t>(lifetime_seconds) * 1000;
        refresh_after_ms_ = expires_at_ms_ - margin_seconds * 1000;
    }

    bool NeedsRefresh(int64_t now_ms) const {
        return refresh_after_ms_ == 0 || now_ms < 0 || now_ms >= refresh_after_ms_;
    }

    int64_t expires_at_ms() const { return expires_at_ms_; }

private:
    int64_t refresh_after_ms_ = 0;
    int64_t expires_at_ms_ = 0;
};

}  // namespace rodakos
