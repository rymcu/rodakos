#pragma once
#include <cstddef>
namespace audio_host {
// Fail the nth bulk audio read (metadata probes are under 64 bytes).
void FailRead(unsigned number, bool io_error, size_t short_bytes = 0);
void ResetReadFault();
}
