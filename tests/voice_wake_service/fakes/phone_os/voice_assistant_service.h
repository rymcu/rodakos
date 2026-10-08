#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace rodakos {
enum class VoiceAssistantPhase { kIdle, kSpeaking, kError };
enum class VoiceAssistantTrigger { kWakeWord };
struct VoiceAssistantState {
    VoiceAssistantPhase phase = VoiceAssistantPhase::kIdle;
    bool stopping = false;
    bool focus_active = false;
    std::string message;
};
class VoiceAssistantService {
public:
    VoiceAssistantPhase GetPhaseSnapshot() {
        VoiceAssistantPhase phase;
        std::function<void()> hook;
        {
            std::lock_guard<std::mutex> lock(mutex);
            phase = state.phase;
            hook = get_phase_hook;
        }
        if (hook) hook();
        return phase;
    }
    VoiceAssistantState GetState() {
        VoiceAssistantState snapshot;
        std::function<void()> hook;
        {
            std::lock_guard<std::mutex> lock(mutex);
            snapshot = state;
            hook = get_state_hook;
        }
        if (hook) hook();
        return snapshot;
    }
    void StopInteraction() {
        std::function<void()> hook;
        { std::lock_guard<std::mutex> lock(mutex); state = {}; ++stops; hook = stop_hook; }
        if (hook) hook();
    }
    void StopInteractionIfCurrent(uint32_t) { StopInteraction(); }
    bool InterruptSpeaking() { std::lock_guard<std::mutex> lock(mutex); ++interrupts; return true; }
    bool StartInteraction(VoiceAssistantTrigger, const std::string&, const std::function<bool()>& admit,
                          uint32_t* generation) {
        if (!admit()) return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (generation) *generation = ++starts;
        return true;
    }
    std::mutex mutex;
    VoiceAssistantState state;
    std::function<void()> get_state_hook;
    std::function<void()> get_phase_hook;
    std::function<void()> stop_hook;
    unsigned starts = 0;
    unsigned stops = 0;
    unsigned interrupts = 0;
};
}
