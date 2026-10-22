#pragma once

#include <string>

namespace rodakos {

class AudioOutputService;

// `wake_echo <seconds> <attenuation_db>` plays speech-band interference through the speaker
// at that many dB below full scale while wake monitoring stays armed, so wake evaluations
// can run against a real echo on the ES8311 loopback. `wake_echo 0 0` stops it early.
// Returns false when the speaker is already owned. Playback is diagnostic only and does not
// take audio focus; the assistant stops it before it opens the speaker.
bool StartWakeEchoDiagnostic(AudioOutputService& output, const std::string& arguments);
void StopWakeEchoDiagnostic(AudioOutputService& output);
bool IsWakeEchoDiagnosticRunning();

}  // namespace rodakos
