#pragma once
#include <cstdint>
namespace rodakos {
class AudioCodecInput {
public:
    enum class InputGainProfile { kUniform, kAecReference10Db };
    AudioCodecInput();
    ~AudioCodecInput();
    bool Init();
    void Deinit();
    bool Open(uint32_t, uint16_t, uint16_t, int, uint16_t = 0);
    void Close();
    bool Read(void*, int);
    bool SetGain(int);
    bool OpenForOwner(const char*, int, uint32_t, uint16_t, uint16_t, int, uint16_t = 0,
                      InputGainProfile = InputGainProfile::kUniform);
    void CloseForOwner(const char*);
    bool ReadForOwner(const char*, void*, int);
    bool IsReady() const { return true; }
    bool IsOpen() const;
};
}
