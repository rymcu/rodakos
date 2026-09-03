#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rodakos {

enum class VoiceListeningMode {
    kAutoStop,
    kManualStop,
    kRealtime,
};

enum class VoiceAbortReason {
    kNone,
    kWakeWordDetected,
};

struct VoiceAudioPacket {
    int sample_rate = 0;
    int frame_duration_ms = 0;
    uint32_t timestamp_ms = 0;
    std::vector<uint8_t> payload;
};

enum class VoiceInboundEventType {
    kSpeakingStarted,
    kAudio,
    kSpeakingStopped,
    kSessionFinished,
    kMcp,
    kError,
};

struct VoiceInboundEvent {
    VoiceInboundEventType type = VoiceInboundEventType::kError;
    uint32_t transport_generation = 0;
    VoiceAudioPacket audio;
    std::string payload;
};

using VoiceInboundHandler = std::function<void(VoiceInboundEvent&&)>;
using VoiceOpenGuard = std::function<bool()>;

class VoiceAssistantTransport {
public:
    virtual ~VoiceAssistantTransport() = default;

    virtual bool Start() = 0;
    virtual bool OpenAudioChannel(VoiceOpenGuard can_continue = {}) = 0;
    virtual void CloseAudioChannel() = 0;
    virtual void WaitForAudioChannelClosed() = 0;
    virtual bool IsAudioChannelOpen() const = 0;
    virtual uint32_t connection_generation() const = 0;

    virtual bool SendAudio(const VoiceAudioPacket& packet, uint32_t expected_generation) = 0;
    virtual bool SendStartListening(VoiceListeningMode mode, uint32_t expected_generation) = 0;
    virtual bool SendStopListening(uint32_t expected_generation) = 0;
    virtual bool SendWakeWordDetected(const std::string& wake_word,
                                      uint32_t expected_generation) = 0;
    virtual bool SendAbortSpeaking(VoiceAbortReason reason, uint32_t expected_generation) = 0;
    virtual bool SendMcpMessage(const std::string& payload, uint32_t expected_generation) = 0;
    virtual void SetInboundHandler(VoiceInboundHandler handler) = 0;

    virtual const char* name() const = 0;
    virtual std::string last_error() const = 0;
};

class NoopVoiceAssistantTransport final : public VoiceAssistantTransport {
public:
    bool Start() override;
    bool OpenAudioChannel(VoiceOpenGuard can_continue = {}) override;
    void CloseAudioChannel() override;
    void WaitForAudioChannelClosed() override;
    bool IsAudioChannelOpen() const override;
    uint32_t connection_generation() const override { return 0; }

    bool SendAudio(const VoiceAudioPacket& packet, uint32_t expected_generation) override;
    bool SendStartListening(VoiceListeningMode mode, uint32_t expected_generation) override;
    bool SendStopListening(uint32_t expected_generation) override;
    bool SendWakeWordDetected(const std::string& wake_word,
                              uint32_t expected_generation) override;
    bool SendAbortSpeaking(VoiceAbortReason reason, uint32_t expected_generation) override;
    bool SendMcpMessage(const std::string& payload, uint32_t expected_generation) override;
    void SetInboundHandler(VoiceInboundHandler handler) override;

    const char* name() const override { return "offline"; }
    std::string last_error() const override { return "Voice transport not configured"; }

private:
    bool Reject(const char* operation);
};

}  // namespace rodakos
