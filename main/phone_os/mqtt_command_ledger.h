#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace rodakos {

// All methods share the caller's MQTT authority lock. This is a bounded replay
// window, not persistent or unbounded exactly-once execution.
class MqttCommandLedger {
public:
    static constexpr size_t kCapacity = 64;
    static constexpr size_t kMaxCommandNoBytes = 128;
    static constexpr size_t kMaxPayloadBytes = 256 * 1024;
    static constexpr size_t kResponseBudgetBytes = 64 * 1024;

    struct Ticket {
        uint64_t authority = 0;
        uint64_t sequence = 0;
    };
    // A zero nonce leaves ordinary/legacy results unscoped without another
    // allocation. Exact stream Stop results retain their original instance.
    struct CompletionScope {
        uint32_t client_generation = 0;
        uint64_t connection_epoch = 0;
        uint64_t stream_instance_nonce = 0;
    };
    enum class Disposition { kExecute, kReplay, kPending, kReject };
    struct Admission {
        Disposition disposition = Disposition::kReject;
        Ticket ticket;
        std::string acknowledgement;
        CompletionScope completion_scope{};
    };

    Admission Begin(const std::string& command_no, const std::string& payload);
    bool Complete(Ticket ticket, const std::string& acknowledgement);
    bool Complete(Ticket ticket, const std::string& acknowledgement, CompletionScope scope);
    void ResetAuthority();

private:
    struct Entry {
        Ticket ticket;
        std::string command_no;
        std::array<unsigned char, 32> payload_hash;
        bool complete = false;
        bool has_response = false;
        std::string response;
        CompletionScope completion_scope{};
    };
    std::deque<Entry> entries_;
    uint64_t authority_ = 1;
    uint64_t next_sequence_ = 0;
    size_t response_bytes_ = 0;
};

}  // namespace rodakos
