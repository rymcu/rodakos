#pragma once

#include "esp_mn_iface.h"
#include "rodakos_adapters/audio_codec_input.h"

#include <cstddef>
#include <string>

namespace rodakos_test::voice_frontend {
void Reset();
void SetCommandUpdateResult(bool succeeds);
void SetCommandClearResult(bool succeeds);
void SetCommandAddResult(bool succeeds);
void SetModelCreateResult(bool succeeds);
void SetDetection(bool detected);
bool LastDetectionRan();
size_t ModelCreateCount();
size_t ModelDestroyCount();
const std::string& RegisteredCommand();
}  // namespace rodakos_test::voice_frontend
