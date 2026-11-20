#include "phone_os/web_access_guard.h"

#include <utility>

namespace rodakos {
namespace {

constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford Base32
constexpr size_t kTicketLength = 16;

// Constant-time comparison: the time taken does not reveal how many leading characters match.
bool SameSecret(const std::string& left, const std::string& right) {
    if (left.size() != right.size() || left.empty()) return false;
    unsigned char diff = 0;
    for (size_t index = 0; index < left.size(); ++index) {
        diff |= static_cast<unsigned char>(left[index] ^ right[index]);
    }
    return diff == 0;
}

}  // namespace

WebAccessGuard::WebAccessGuard(std::function<uint32_t()> random32, std::function<int64_t()> now_ms)
    : random32_(std::move(random32)), now_ms_(std::move(now_ms)) {}

std::string WebAccessGuard::RandomCode(size_t length) {
    std::string out;
    out.reserve(length);
    uint32_t bits = 0;
    int available = 0;
    while (out.size() < length) {
        if (available < 5) {
            bits = random32_();
            available = 32;
        }
        out.push_back(kAlphabet[bits & 31u]);
        bits >>= 5;
        available -= 5;
    }
    return out;
}

void WebAccessGuard::Reset() {
    code_ = RandomCode(kCodeLength);
    clients_ = {};
    tickets_ = {};
}

void WebAccessGuard::Clear() {
    code_.clear();
    clients_ = {};
    tickets_ = {};
}

std::string WebAccessGuard::DisplayCode() const {
    if (code_.size() != kCodeLength) return code_;
    return code_.substr(0, 4) + " " + code_.substr(4);
}

std::string WebAccessGuard::Normalize(const std::string& presented) {
    std::string out;
    out.reserve(presented.size());
    for (const char raw : presented) {
        if (raw == ' ' || raw == '-' || raw == '\t') continue;
        char ch = raw;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        // Crockford decoding maps the excluded look-alike letters to their digits.
        if (ch == 'I' || ch == 'L') ch = '1';
        if (ch == 'O') ch = '0';
        out.push_back(ch);
    }
    return out;
}

WebAccessGuard::Client* WebAccessGuard::FindClient(uint32_t client, bool create) {
    Client* oldest = &clients_[0];
    for (auto& entry : clients_) {
        if (entry.used && entry.address == client) return &entry;
    }
    if (!create) return nullptr;
    for (auto& entry : clients_) {
        if (!entry.used) {
            entry = Client{client, 0, 0, 0, true};
            return &entry;
        }
        // Evict the least recent client that is not currently locked, so a flood of new
        // addresses cannot release a lock early.
        const int64_t now = now_ms_();
        const bool entry_locked = entry.locked_until_ms > now;
        const bool oldest_locked = oldest->locked_until_ms > now;
        if ((oldest_locked && !entry_locked) ||
            (entry_locked == oldest_locked && entry.last_seen_ms < oldest->last_seen_ms)) {
            oldest = &entry;
        }
    }
    *oldest = Client{client, 0, 0, 0, true};
    return oldest;
}

const WebAccessGuard::Client* WebAccessGuard::FindClient(uint32_t client) const {
    for (const auto& entry : clients_) {
        if (entry.used && entry.address == client) return &entry;
    }
    return nullptr;
}

WebAccessGuard::Result WebAccessGuard::Check(uint32_t client, const std::string& presented) {
    const int64_t now = now_ms_();
    if (const Client* known = FindClient(client); known != nullptr && known->locked_until_ms > now) {
        return Result::kLocked;
    }
    if (presented.empty()) return Result::kMissing;

    if (!code_.empty() && SameSecret(Normalize(presented), code_)) {
        if (Client* entry = FindClient(client, false); entry != nullptr) {
            entry->failures = 0;
            entry->last_seen_ms = now;
        }
        return Result::kAccepted;
    }

    Client* entry = FindClient(client, true);
    entry->last_seen_ms = now;
    if (++entry->failures >= kMaxFailures) {
        entry->failures = 0;
        entry->locked_until_ms = now + kLockoutMs;
        return Result::kLocked;
    }
    return Result::kRejected;
}

int WebAccessGuard::RetryAfterSeconds(uint32_t client) const {
    const Client* entry = FindClient(client);
    if (entry == nullptr) return 0;
    const int64_t remaining = entry->locked_until_ms - now_ms_();
    return remaining <= 0 ? 0 : static_cast<int>((remaining + 999) / 1000);
}

std::string WebAccessGuard::IssueTicket(const std::string& path) {
    const int64_t now = now_ms_();
    Ticket* slot = nullptr;
    for (auto& ticket : tickets_) {
        if (ticket.value.empty() || ticket.expires_ms <= now) {
            slot = &ticket;
            break;
        }
    }
    if (slot == nullptr) {
        // All in use: replace the one closest to expiry.
        slot = &tickets_[0];
        for (auto& ticket : tickets_) {
            if (ticket.expires_ms < slot->expires_ms) slot = &ticket;
        }
    }
    slot->value = RandomCode(kTicketLength);
    slot->path = path;
    slot->expires_ms = now + kTicketLifetimeMs;
    return slot->value;
}

bool WebAccessGuard::RedeemTicket(const std::string& ticket, const std::string& path) {
    const int64_t now = now_ms_();
    for (auto& entry : tickets_) {
        if (entry.value.empty() || !SameSecret(entry.value, ticket)) continue;
        const bool valid = entry.expires_ms > now && entry.path == path;
        entry = Ticket{};
        return valid;
    }
    return false;
}

}  // namespace rodakos
