#pragma once

#include "esp_mn_iface.h"
#include "rodakos_adapters/audio_codec_input.h"

#include <cstddef>
#include <string>

namespace rodakos_test::voice_frontend {
struct CommandRegistryStats {
    size_t allocations = 0;
    size_t reallocations = 0;
    size_t frees = 0;
    size_t free_without_registry = 0;
};
void Reset();
void ResetMultiNet();
void SetCommandUpdateResult(bool succeeds);
void SetCommandClearResult(bool succeeds);
void SetCommandAddResult(bool succeeds);
void SetModelCreateResult(bool succeeds);
void SetModelChunkSamples(int samples);
void SetSelectedModel(const char* name);
CommandRegistryStats RegistryStats();
bool RegistryMatchesModel();
bool RegistryPresent();
void SetDetection(bool detected);
bool LastDetectionRan();
size_t ModelCreateCount();
size_t ModelDestroyCount();
const std::string& RegisteredCommand();
void SupplyAudioReads(size_t count);
size_t AfeFeedCount();
void* LastAfeFeedBuffer();
bool AfeFeedBufferReleased();
void ObserveAfeFeedBuffer(void* pointer);
void ObserveDelete(void* pointer);
void ResetAllocationObserver();
}  // namespace rodakos_test::voice_frontend
