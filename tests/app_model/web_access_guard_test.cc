#include "test_framework.h"

#include "phone_os/web_access_guard.h"

#include <cstdint>
#include <memory>
#include <set>
#include <string>

namespace {

using rodakos::WebAccessGuard;
using Result = rodakos::WebAccessGuard::Result;

struct Clock {
    int64_t now = 1000;
};

WebAccessGuard MakeGuard(Clock& clock, uint32_t seed = 0x9e3779b9u) {
    auto state = std::make_shared<uint32_t>(seed);
    return WebAccessGuard(
        [state]() {
            // xorshift32: deterministic, varied bits for the tests.
            uint32_t x = *state;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            *state = x;
            return x;
        },
        [&clock]() { return clock.now; });
}

constexpr uint32_t kPc = 0xc0a85864;     // 192.168.88.100
constexpr uint32_t kOther = 0xc0a85865;  // 192.168.88.101

}  // namespace

RODAK_TEST("Web access code is 8 Crockford Base32 characters shown in two groups") {
    Clock clock;
    auto guard = MakeGuard(clock);
    guard.Reset();
    const std::string code = guard.code();
    RODAK_CHECK_EQ(code.size(), WebAccessGuard::kCodeLength);
    for (const char ch : code) {
        RODAK_CHECK(std::string("0123456789ABCDEFGHJKMNPQRSTVWXYZ").find(ch) != std::string::npos);
    }
    RODAK_CHECK_EQ(guard.DisplayCode(), code.substr(0, 4) + " " + code.substr(4));
}

RODAK_TEST("Web access code changes on every new session") {
    Clock clock;
    auto guard = MakeGuard(clock);
    std::set<std::string> codes;
    for (int index = 0; index < 20; ++index) {
        guard.Reset();
        codes.insert(guard.code());
    }
    RODAK_CHECK_EQ(codes.size(), static_cast<size_t>(20));
}

RODAK_TEST("Web access code accepts case, spaces, dashes and Crockford look-alikes") {
    Clock clock;
    auto guard = MakeGuard(clock);
    guard.Reset();
    const std::string code = guard.code();
    std::string lower = code;
    for (char& ch : lower) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    RODAK_CHECK(guard.Check(kPc, lower) == Result::kAccepted);
    RODAK_CHECK(guard.Check(kPc, guard.DisplayCode()) == Result::kAccepted);
    RODAK_CHECK(guard.Check(kPc, code.substr(0, 4) + "-" + code.substr(4)) == Result::kAccepted);
    RODAK_CHECK_EQ(WebAccessGuard::Normalize("o1lI x"), std::string("0111X"));
}

RODAK_TEST("Web access rejects a missing or wrong code") {
    Clock clock;
    auto guard = MakeGuard(clock);
    guard.Reset();
    RODAK_CHECK(guard.Check(kPc, "") == Result::kMissing);
    RODAK_CHECK(guard.Check(kPc, "ZZZZZZZZ") == Result::kRejected);
    RODAK_CHECK(guard.Check(kPc, guard.code() + "0") == Result::kRejected);
}

RODAK_TEST("Web access locks only the client that failed five times") {
    Clock clock;
    auto guard = MakeGuard(clock);
    guard.Reset();
    for (int attempt = 0; attempt < 4; ++attempt) {
        RODAK_CHECK(guard.Check(kPc, "ZZZZZZZZ") == Result::kRejected);
    }
    RODAK_CHECK(guard.Check(kPc, "ZZZZZZZZ") == Result::kLocked);
    // Even the right code is refused while locked.
    RODAK_CHECK(guard.Check(kPc, guard.code()) == Result::kLocked);
    RODAK_CHECK_EQ(guard.RetryAfterSeconds(kPc), 30);
    // Another computer is not affected.
    RODAK_CHECK(guard.Check(kOther, guard.code()) == Result::kAccepted);

    clock.now += WebAccessGuard::kLockoutMs;
    RODAK_CHECK_EQ(guard.RetryAfterSeconds(kPc), 0);
    RODAK_CHECK(guard.Check(kPc, guard.code()) == Result::kAccepted);
}

RODAK_TEST("Web access success clears the failure count") {
    Clock clock;
    auto guard = MakeGuard(clock);
    guard.Reset();
    for (int attempt = 0; attempt < 4; ++attempt) guard.Check(kPc, "ZZZZZZZZ");
    RODAK_CHECK(guard.Check(kPc, guard.code()) == Result::kAccepted);
    for (int attempt = 0; attempt < 4; ++attempt) {
        RODAK_CHECK(guard.Check(kPc, "ZZZZZZZZ") == Result::kRejected);
    }
}

RODAK_TEST("Web access flood of new addresses cannot evict a locked client") {
    Clock clock;
    auto guard = MakeGuard(clock);
    guard.Reset();
    for (int attempt = 0; attempt < 5; ++attempt) guard.Check(kPc, "ZZZZZZZZ");
    for (uint32_t address = 1; address <= 40; ++address) guard.Check(address, "ZZZZZZZZ");
    RODAK_CHECK(guard.Check(kPc, guard.code()) == Result::kLocked);
}

RODAK_TEST("Web access accepts nothing after the session ends") {
    Clock clock;
    auto guard = MakeGuard(clock);
    guard.Reset();
    const std::string old_code = guard.code();
    guard.Clear();
    RODAK_CHECK(guard.Check(kPc, old_code) == Result::kRejected);
    guard.Reset();
    RODAK_CHECK(guard.Check(kOther, old_code) == Result::kRejected);
}

RODAK_TEST("Web download tickets work once, for one path, before they expire") {
    Clock clock;
    auto guard = MakeGuard(clock);
    guard.Reset();
    const std::string ticket = guard.IssueTicket("/Music/a.mp3");
    RODAK_CHECK_EQ(ticket.size(), static_cast<size_t>(16));
    RODAK_CHECK_FALSE(guard.RedeemTicket(ticket, "/Music/b.mp3"));
    // A wrong-path attempt burns the ticket.
    RODAK_CHECK_FALSE(guard.RedeemTicket(ticket, "/Music/a.mp3"));

    const std::string second = guard.IssueTicket("/Music/a.mp3");
    RODAK_CHECK(guard.RedeemTicket(second, "/Music/a.mp3"));
    RODAK_CHECK_FALSE(guard.RedeemTicket(second, "/Music/a.mp3"));

    const std::string late = guard.IssueTicket("/x.bin");
    clock.now += WebAccessGuard::kTicketLifetimeMs;
    RODAK_CHECK_FALSE(guard.RedeemTicket(late, "/x.bin"));
    RODAK_CHECK_FALSE(guard.RedeemTicket("", "/x.bin"));
}

RODAK_TEST("Web download tickets are cleared with the session") {
    Clock clock;
    auto guard = MakeGuard(clock);
    guard.Reset();
    const std::string ticket = guard.IssueTicket("/x.bin");
    guard.Reset();
    RODAK_CHECK_FALSE(guard.RedeemTicket(ticket, "/x.bin"));
}
