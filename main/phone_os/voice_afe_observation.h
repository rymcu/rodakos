#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>

namespace rodakos {

enum class AfeProducerStage : uint32_t {
    kUnknown,
    kInputOpen,
    kRawRead,
    kReadReturned,
    kReadFailed,
    kFeedAdmission,
    kPrepareFeed,
    kFeedAdmitted,
    kApiBoundary,
    kReturnedWaitPublish,
    kCreditPublished,
    kBetweenReads,
};

inline uint32_t AfeElapsedUs(int64_t begin, int64_t end) {
    if (end <= begin) return 0;
    const auto elapsed = static_cast<uint64_t>(end) - static_cast<uint64_t>(begin);
    return elapsed >= UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(elapsed);
}

struct AfeProducerObservation {
    uint32_t generation = 0;
    uint32_t epoch = 0;
    AfeProducerStage stage = AfeProducerStage::kUnknown;
    uint32_t sequence = 0;
    int64_t began_us = 0;
    int32_t detail = 0;
    uint32_t previous_elapsed_us = 0;
    uint32_t max_read_us = 0;
    uint32_t max_read_sequence = 0;
    uint32_t max_api_us = 0;
    uint32_t max_api_sequence = 0;
    uint32_t max_return_to_publish_us = 0;
    uint32_t max_publish_sequence = 0;
};
static_assert(sizeof(AfeProducerObservation) == 56);

// Capture alone publishes. Lifecycle/consumer readers never reset an older raw read's scope.
// This lock is independent of the frontend mutex needed to publish SDK output credits.
class AfeProducerDiagnostics {
public:
    void Publish(AfeProducerStage stage, uint32_t generation, uint32_t epoch,
                 uint32_t sequence, int64_t began_us, int32_t detail = 0,
                 uint32_t previous_elapsed_us = 0) {
        portENTER_CRITICAL(&mux_);
        if (state_.generation != generation || state_.epoch != epoch) state_ = {};
        state_.generation = generation;
        state_.epoch = epoch;
        state_.stage = stage;
        state_.sequence = sequence;
        state_.began_us = began_us;
        state_.detail = detail;
        state_.previous_elapsed_us = previous_elapsed_us;
        if (stage == AfeProducerStage::kReadReturned || stage == AfeProducerStage::kReadFailed)
            UpdateMaximum(previous_elapsed_us, sequence, state_.max_read_us, state_.max_read_sequence);
        if (stage == AfeProducerStage::kReturnedWaitPublish)
            UpdateMaximum(previous_elapsed_us, sequence, state_.max_api_us, state_.max_api_sequence);
        if (stage == AfeProducerStage::kCreditPublished)
            UpdateMaximum(previous_elapsed_us, sequence, state_.max_return_to_publish_us,
                          state_.max_publish_sequence);
        portEXIT_CRITICAL(&mux_);
    }

    AfeProducerObservation Snapshot() const {
        portENTER_CRITICAL(&mux_);
        const AfeProducerObservation result = state_;
        portEXIT_CRITICAL(&mux_);
        return result;
    }

private:
    static void UpdateMaximum(uint32_t elapsed, uint32_t sequence, uint32_t& maximum,
                              uint32_t& maximum_sequence) {
        if (elapsed > maximum) {
            maximum = elapsed;
            maximum_sequence = sequence;
        }
    }
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    AfeProducerObservation state_;
};

}  // namespace rodakos
