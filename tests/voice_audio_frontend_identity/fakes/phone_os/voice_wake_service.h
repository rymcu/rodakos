#pragma once
#include "phone_os/voice_identity.h"
#include <functional>
#include <string>
namespace rodakos {
class VoiceWakeRuntime {
public:
    virtual ~VoiceWakeRuntime() = default;
    virtual bool Init() = 0;
    virtual void Deinit() = 0;
    virtual bool StartListening(std::function<void(const std::string&)>) = 0;
    virtual void StopListening() = 0;
    virtual bool IsListening() const = 0;
    virtual bool IsAvailable() const = 0;
    virtual bool ConfigureWakeWord(const VoiceIdentityConfig&) = 0;
    virtual const char* name() const = 0;
    virtual const char* last_error() const = 0;
    virtual std::string LastErrorSnapshot() const { return last_error(); }
};
}
