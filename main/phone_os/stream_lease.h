#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace rodakos {

class StreamLease {
public:
    StreamLease(uint32_t generation, uint64_t epoch, uint64_t nonce, std::string session,
                std::string start_command = {})
        : client_generation(generation), connection_epoch(epoch), instance_nonce(nonce),
          session_id(std::move(session)), start_command_no(std::move(start_command)) {}

    const uint32_t client_generation;
    const uint64_t connection_epoch;
    const uint64_t instance_nonce;
    const std::string session_id;
    const std::string start_command_no;

    bool IsActive() const { return active_.load(); }
    void Revoke() { active_.store(false); }
    bool WasStartAccepted() const { return start_accepted_.load(); }
    void MarkStartAccepted() { start_accepted_.store(true); }

    // The final atomic read admits this one operation. Revocation never waits
    // for UI/peer locks; an operation admitted beforehand may finish, while
    // delayed callbacks must obtain their own admission immediately before use.
    template <typename F>
    bool TryApply(F&& operation) const {
        if (!active_.load()) return false;
        std::forward<F>(operation)();
        return true;
    }

private:
    std::atomic<bool> active_{true};
    std::atomic<bool> start_accepted_{false};
};

using StreamLeasePtr = std::shared_ptr<StreamLease>;

}  // namespace rodakos
