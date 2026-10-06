#include "service_fixture.h"
#include "phone_os/mqtt_command_ledger.h"
#include "rodak_sha256.h"

namespace {
using Ledger = rodakos::MqttCommandLedger;
using Disposition = Ledger::Disposition;
void Rejected(const Ledger::Admission& result, const std::string& expected) {
    RODAK_CHECK(result.disposition == Disposition::kReject);
    const auto body = mqtt_host::Parse(result.acknowledgement);
    const char* code = cJSON_GetStringValue(mqtt_host::Get(body.get(), "errorCode"));
    RODAK_CHECK(code != nullptr);
    RODAK_CHECK_EQ(std::string(code), expected);
}
}

RODAK_TEST("Command ledger uses the real PSA SHA256 implementation and full raw payload bytes") {
    rodakos::Sha256 hash;
    std::array<unsigned char, 32> digest{};
    RODAK_CHECK(hash.Start());
    RODAK_CHECK(hash.Update("abc", 3));
    RODAK_CHECK(hash.Finish(digest));
    RODAK_CHECK_EQ(rodakos::Sha256ToHex(digest),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Ledger ledger;
    const auto first = ledger.Begin("hash-identity", "ping");
    RODAK_CHECK(first.disposition == Disposition::kExecute);
    Rejected(ledger.Begin("hash-identity", "\"ping\""), "command_conflict");
    Rejected(ledger.Begin("hash-identity", std::string("ping\0x", 6)), "command_conflict");
}

RODAK_TEST("Command ledger protects pending admission and freezes first completion") {
    Ledger ledger;
    const auto first = ledger.Begin("first", "ping");
    RODAK_CHECK(first.disposition == Disposition::kExecute);
    RODAK_CHECK(ledger.Begin("first", "ping").disposition == Disposition::kPending);
    Rejected(ledger.Begin("first", "reboot"), "command_conflict");
    RODAK_CHECK(ledger.Complete(first.ticket, "first-result"));
    RODAK_CHECK_FALSE(ledger.Complete(first.ticket, "late-result"));
    const auto replay = ledger.Begin("first", "ping");
    RODAK_CHECK(replay.disposition == Disposition::kReplay);
    RODAK_CHECK_EQ(replay.acknowledgement, "first-result");
}

RODAK_TEST("Command ledger caches failed ACKs exactly like successful results") {
    Ledger ledger;
    const auto first = ledger.Begin("failure", "reboot");
    const std::string acknowledgement = "{\"status\":\"error\",\"errorCode\":\"unsupported_command\"}";
    RODAK_CHECK(ledger.Complete(first.ticket, acknowledgement));
    const auto replay = ledger.Begin("failure", "reboot");
    RODAK_CHECK(replay.disposition == Disposition::kReplay);
    RODAK_CHECK_EQ(replay.acknowledgement, acknowledgement);
}

RODAK_TEST("Command ledger authority reset invalidates old tickets even for reused command numbers") {
    Ledger ledger;
    const auto old = ledger.Begin("reused", "ping");
    ledger.ResetAuthority();
    const auto current = ledger.Begin("reused", "reboot");
    RODAK_CHECK(current.disposition == Disposition::kExecute);
    RODAK_CHECK_FALSE(ledger.Complete(old.ticket, "stale"));
    RODAK_CHECK(ledger.Begin("reused", "reboot").disposition == Disposition::kPending);
    RODAK_CHECK(ledger.Complete(current.ticket, "current"));
    RODAK_CHECK_EQ(ledger.Begin("reused", "reboot").acknowledgement, "current");
}

RODAK_TEST("Command ledger never evicts in-flight entries and recovers capacity after completion") {
    Ledger ledger;
    std::vector<Ledger::Ticket> tickets;
    for (size_t index = 0; index < Ledger::kCapacity; ++index) {
        const auto item = ledger.Begin("pending-" + std::to_string(index), "ping");
        RODAK_CHECK(item.disposition == Disposition::kExecute);
        tickets.push_back(item.ticket);
    }
    Rejected(ledger.Begin("overflow", "ping"), "command_capacity_exceeded");
    RODAK_CHECK(ledger.Begin("pending-0", "ping").disposition == Disposition::kPending);
    RODAK_CHECK(ledger.Complete(tickets[1], "done"));
    RODAK_CHECK(ledger.Begin("overflow", "ping").disposition == Disposition::kExecute);
    RODAK_CHECK(ledger.Begin("pending-0", "ping").disposition == Disposition::kPending);
    RODAK_CHECK_FALSE(ledger.Complete(tickets[1], "late"));
}

RODAK_TEST("Command ledger continues past 64 new commands and documents replay after FIFO eviction") {
    Ledger ledger;
    for (unsigned index = 0; index < 70; ++index) {
        const auto item = ledger.Begin("sequence-" + std::to_string(index), "ping");
        RODAK_CHECK(item.disposition == Disposition::kExecute);
        RODAK_CHECK(ledger.Complete(item.ticket, "done-" + std::to_string(index)));
    }
    RODAK_CHECK(ledger.Begin("sequence-6", "ping").disposition == Disposition::kReplay);
    RODAK_CHECK(ledger.Begin("sequence-0", "ping").disposition == Disposition::kExecute);
    // 命中不刷新 FIFO；新 admission 淘汰最早完成条目，窗口之外不承诺一次执行。
    RODAK_CHECK(ledger.Begin("sequence-6", "ping").disposition == Disposition::kExecute);
}

RODAK_TEST("Command ledger keeps completed tombstones for oversized or empty ACK bodies") {
    Ledger ledger;
    const auto large = ledger.Begin("large-result", "ping");
    RODAK_CHECK(ledger.Complete(large.ticket, std::string(Ledger::kResponseBudgetBytes + 1, 'x')));
    Rejected(ledger.Begin("large-result", "ping"), "command_result_unavailable");
    Rejected(ledger.Begin("large-result", "reboot"), "command_conflict");
    RODAK_CHECK_FALSE(ledger.Complete(large.ticket, "smaller-late-result"));
    const auto empty = ledger.Begin("empty-result", "ping");
    RODAK_CHECK(ledger.Complete(empty.ticket, ""));
    Rejected(ledger.Begin("empty-result", "ping"), "command_result_unavailable");
}

RODAK_TEST("Command ledger response byte pressure removes old bodies without reopening side effects") {
    Ledger ledger;
    const auto older = ledger.Begin("older", "ping");
    const auto newer = ledger.Begin("newer", "ping");
    RODAK_CHECK(ledger.Complete(older.ticket, std::string(40 * 1024, 'a')));
    RODAK_CHECK(ledger.Complete(newer.ticket, std::string(30 * 1024, 'b')));
    Rejected(ledger.Begin("older", "ping"), "command_result_unavailable");
    const auto replay = ledger.Begin("newer", "ping");
    RODAK_CHECK(replay.disposition == Disposition::kReplay);
    RODAK_CHECK_EQ(replay.acknowledgement, std::string(30 * 1024, 'b'));
}

RODAK_TEST("Command ledger bounds command numbers and raw payloads before admission") {
    Ledger ledger;
    Rejected(ledger.Begin("", "ping"), "command_limits_exceeded");
    Rejected(ledger.Begin(std::string(129, 'n'), "ping"), "command_limits_exceeded");
    Rejected(ledger.Begin("payload-too-large", std::string(256 * 1024 + 1, 'p')), "command_limits_exceeded");
    const auto boundary = ledger.Begin(std::string(128, 'n'), std::string(256 * 1024, 'p'));
    RODAK_CHECK(boundary.disposition == Disposition::kExecute);
    RODAK_CHECK_FALSE(ledger.Complete({}, "invalid-ticket"));
}
