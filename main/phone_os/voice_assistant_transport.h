#pragma once

#include <cstdint>
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

class VoiceAssistantTransport {
public:
    virtual ~VoiceAssistantTransport() = default;

    virtual bool Start() = 0;
    virtual bool OpenAudioChannel() = 0;
    virtual void CloseAudioChannel() = 0;
    virtual bool IsAudioChannelOpen() const = 0;

    virtual bool SendAudio(const VoiceAudioPacket& packet) = 0;
    virtual bool SendStartListening(VoiceListeningMode mode) = 0;
    virtual bool SendStopListening() = 0;
    virtual bool SendWakeWordDetected(const std::string& wake_word) = 0;
    virtual bool SendAbortSpeaking(VoiceAbortReason reason) = 0;
    virtual bool SendMcpMessage(const std::string& payload) = 0;

    virtual const char* name() const = 0;
    virtual const char* last_error() const = 0;
};

class NoopVoiceAssistantTransport final : public VoiceAssistantTransport {
public:
    bool Start() override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpen() const override;

    bool SendAudio(const VoiceAudioPacket& packet) override;
    bool SendStartListening(VoiceListeningMode mode) override;
    bool SendStopListening() override;
    bool SendWakeWordDetected(const std::string& wake_word) override;
    bool SendAbortSpeaking(VoiceAbortReason reason) override;
    bool SendMcpMessage(const std::string& payload) override;

    const char* name() const override { return "offline"; }
    const char* last_error() const override { return last_error_.c_str(); }

private:
    bool Reject(const char* operation);

    std::string last_error_ = "Voice transport not configured";
};

}  // namespace rodakos
