#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace rodakos {

// Access control for the LAN web file service, free of ESP-IDF so host tests cover it.
//
// - The access code is 8 Crockford Base32 characters (no I, L, O, U): 40 random bits, shown
//   as "K7QM 4RX8". Comparison ignores case, spaces and dashes, and maps the look-alikes
//   I/L -> 1 and O -> 0 the way Crockford decoding does.
// - Failed attempts are counted per client address. Five consecutive failures lock only that
//   address for 30 s; a success clears its count. There is no global lockout.
// - Browser downloads cannot carry a header, so an authenticated client trades the code for a
//   one-time ticket bound to one path, valid for 60 s.
class WebAccessGuard {
public:
    static constexpr size_t kCodeLength = 8;
    static constexpr int kMaxFailures = 5;
    static constexpr int64_t kLockoutMs = 30 * 1000;
    static constexpr int64_t kTicketLifetimeMs = 60 * 1000;
    static constexpr size_t kTrackedClients = 8;
    static constexpr size_t kTickets = 4;

    enum class Result {
        kAccepted,
        kMissing,
        kRejected,
        // The client is cooling down; RetryAfterSeconds() says how long.
        kLocked,
    };

    // `random32` supplies entropy (esp_random on the device); `now_ms` a monotonic clock.
    WebAccessGuard(std::function<uint32_t()> random32, std::function<int64_t()> now_ms);

    // Starts a new session: new code, cleared counters and tickets.
    void Reset();
    // Ends the session: no code is accepted until the next Reset().
    void Clear();
    // "K7QM4RX8"; empty when cleared.
    const std::string& code() const { return code_; }
    // "K7QM 4RX8" for display.
    std::string DisplayCode() const;

    // `client` is the peer address (IPv4 as uint32, any stable key works for tests).
    Result Check(uint32_t client, const std::string& presented);
    int RetryAfterSeconds(uint32_t client) const;

    // Issues a ticket for `path`; empty when the table is exhausted.
    std::string IssueTicket(const std::string& path);
    // Accepts each ticket once, only for the path it was issued for, before it expires.
    bool RedeemTicket(const std::string& ticket, const std::string& path);

    static std::string Normalize(const std::string& presented);

private:
    struct Client {
        uint32_t address = 0;
        int failures = 0;
        int64_t locked_until_ms = 0;
        int64_t last_seen_ms = 0;
        bool used = false;
    };
    struct Ticket {
        std::string value;
        std::string path;
        int64_t expires_ms = 0;
    };

    std::string RandomCode(size_t length);
    Client* FindClient(uint32_t client, bool create);
    const Client* FindClient(uint32_t client) const;

    std::function<uint32_t()> random32_;
    std::function<int64_t()> now_ms_;
    std::string code_;
    std::array<Client, kTrackedClients> clients_{};
    std::array<Ticket, kTickets> tickets_{};
};

}  // namespace rodakos
