#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rodakos {

struct VoiceRecorderConfig {
    int sample_rate = 16000;
    int channels = 1;
    int bits_per_sample = 16;
    int frame_duration_ms = 20;
};

struct VoicePcmFrame {
    VoiceRecorderConfig config;
    uint32_t timestamp_ms = 0;
    std::vector<int16_t> samples;
};

class VoiceRecorderService {
public:
    virtual ~VoiceRecorderService() = default;

    virtual bool Init() = 0;
    virtual void Deinit() = 0;
    virtual bool Start(const VoiceRecorderConfig& config) = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual bool PopFrame(VoicePcmFrame& frame) = 0;
    virtual const char* name() const = 0;
    virtual const char* last_error() const = 0;
};

class NoopVoiceRecorderService final : public VoiceRecorderService {
public:
    bool Init() override;
    void Deinit() override;
    bool Start(const VoiceRecorderConfig& config) override;
    void Stop() override;
    bool IsRunning() const override;
    bool PopFrame(VoicePcmFrame& frame) override;
    const char* name() const override { return "offline"; }
    const char* last_error() const override { return last_error_.c_str(); }

private:
    bool initialized_ = false;
    std::string last_error_ = "Voice recorder not configured";
};

}  // namespace rodakos
