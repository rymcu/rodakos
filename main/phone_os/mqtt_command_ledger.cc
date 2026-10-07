#include "phone_os/mqtt_command_ledger.h"
#include "rodak_sha256.h"

#include <algorithm>
#include <limits>

namespace rodakos {
namespace {
MqttCommandLedger::Admission Reject(const char* code) {
    return {MqttCommandLedger::Disposition::kReject, {},
            std::string("{\"status\":\"error\",\"errorCode\":\"") + code + "\"}"};
}
}

MqttCommandLedger::Admission MqttCommandLedger::Begin(
    const std::string& command_no, const std::string& payload) {
    if (command_no.empty() || command_no.size() > kMaxCommandNoBytes ||
        payload.size() > kMaxPayloadBytes) return Reject("command_limits_exceeded");
    Sha256 hash;
    std::array<unsigned char, 32> digest{};
    if (!hash.Start() || !hash.Update(payload.data(), payload.size()) || !hash.Finish(digest))
        return Reject("command_hash_failed");
    for (const auto& entry : entries_) {
        if (entry.command_no != command_no) continue;
        if (entry.payload_hash != digest) return Reject("command_conflict");
        if (!entry.complete) return {Disposition::kPending, entry.ticket, {}};
        if (!entry.has_response) return Reject("command_result_unavailable");
        return {Disposition::kReplay, entry.ticket, entry.response, entry.completion_scope};
    }
    if (next_sequence_ == std::numeric_limits<uint64_t>::max())
        return Reject("command_capacity_exceeded");
    if (entries_.size() == kCapacity) {
        const auto completed = std::find_if(entries_.begin(), entries_.end(),
                                            [](const Entry& entry) { return entry.complete; });
        if (completed == entries_.end()) return Reject("command_capacity_exceeded");
        response_bytes_ -= completed->response.size();
        entries_.erase(completed);
    }
    const Ticket ticket{authority_, ++next_sequence_};
    entries_.push_back({ticket, command_no, digest, false, false, {}});
    return {Disposition::kExecute, ticket, {}};
}

bool MqttCommandLedger::Complete(Ticket ticket, const std::string& acknowledgement) {
    return Complete(ticket, acknowledgement, {});
}

bool MqttCommandLedger::Complete(Ticket ticket, const std::string& acknowledgement,
                                 CompletionScope scope) {
    if (ticket.authority != authority_) return false;
    const auto current = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.ticket.sequence == ticket.sequence;
    });
    if (current == entries_.end() || current->complete) return false;
    current->complete = true;
    current->completion_scope = scope;
    if (acknowledgement.empty() || acknowledgement.size() > kResponseBudgetBytes) return true;
    // Preserve remembered command IDs even when large replies consume the byte
    // budget. Replaying such a tombstone cannot re-enter the side-effect handler.
    for (auto& entry : entries_) {
        if (response_bytes_ <= kResponseBudgetBytes - acknowledgement.size()) break;
        response_bytes_ -= entry.response.size();
        std::string().swap(entry.response);
        entry.has_response = false;
    }
    current->response = acknowledgement;
    current->has_response = true;
    response_bytes_ += acknowledgement.size();
    return true;
}

void MqttCommandLedger::ResetAuthority() {
    entries_.clear();
    response_bytes_ = 0;
    if (++authority_ == 0) ++authority_;
    // Ticket sequences never reset, so a delayed completion cannot match a new
    // command even when both authority and command number are replaced.
}

}  // namespace rodakos
