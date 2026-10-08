#pragma once

// This USB diagnostic is absent from ordinary firmware, including its dispatcher.
#ifdef RODAKOS_RELEASE_TESTS

#include <cstdint>
#include <mutex>
#include <string_view>

namespace rodakos {

struct VoiceLifecycleTasks {
    bool assistant = false;
    bool capture = false;
    bool supervisor = false;
};

enum class VoiceLifecycleAssistantPhase { kUnknown, kIdle, kConnecting, kListening, kSpeaking, kError };

struct VoiceLifecycleSnapshot {
    VoiceLifecycleTasks tasks;
    VoiceLifecycleAssistantPhase assistant_phase = VoiceLifecycleAssistantPhase::kUnknown;
    bool assistant_stopping = false;
    uint64_t uptime_ms = 0;
    uint32_t internal_free = 0;
    uint32_t internal_min = 0;
    uint32_t internal_largest = 0;
    uint32_t psram_free = 0;
    uint32_t psram_largest = 0;
};

struct VoiceLifecycleWakeState {
    bool enabled = false;
    bool listening = false;
};

struct VoiceLifecycleEvent {
    uint32_t id = 0;
    const char* phase = "rejected";
    const char* result = "none";
    const char* reason = "none";
    const char* scope = "unknown";
    bool has_snapshot = false;
    VoiceLifecycleSnapshot snapshot{};
    VoiceLifecycleAssistantPhase assistant_phase_before = VoiceLifecycleAssistantPhase::kUnknown;
    bool assistant_stopping_before = false;
    bool enabled_before = false;
    bool enabled_after = false;
    bool listening_after = false;
    bool stopped = false;
    bool restart_returned = false;
    bool recovered = false;
};

// Callbacks run outside the slot mutex. Construct and wire these before serial
// Start; dependencies and their context must outlive this permanent controller.
struct VoiceLifecycleDependencies {
    void* context = nullptr;
    const char* (*busy_reason)(void*) = nullptr;
    VoiceLifecycleSnapshot (*snapshot)(void*) = nullptr;
    VoiceLifecycleWakeState (*wake_state)(void*) = nullptr;
    void (*deinit)(void*) = nullptr;
    bool (*restart)(void*) = nullptr;
    void (*emit)(void*, const VoiceLifecycleEvent&) = nullptr;
};

class VoiceLifecycleDiagnostic {
public:
    explicit VoiceLifecycleDiagnostic(VoiceLifecycleDependencies dependencies);
    // True means this exact diagnostic verb was handled, including rejection.
    bool HandleSerialLine(std::string_view line);
    bool BlocksSerialMutation(std::string_view line) const;
    bool IsBusy() const;
    // One claim, one Deinit, at most one restart. Called only by permanent main.
    // Synchronous service Join has no hard completion deadline.
    bool Pump();

private:
    enum class Slot { kIdle, kAccepting, kPending, kExecuting };
    void Enqueue(uint32_t id);
    void Emit(const VoiceLifecycleEvent& event) const;
    void CompleteSlot();
    VoiceLifecycleDependencies dependencies_;
    mutable std::mutex mutex_;
    Slot slot_ = Slot::kIdle;
    uint32_t pending_id_ = 0;
    uint32_t accepted_id_watermark_ = 0;
};

void PrintVoiceLifecycleEvent(void*, const VoiceLifecycleEvent& event);

}  // namespace rodakos
#endif
