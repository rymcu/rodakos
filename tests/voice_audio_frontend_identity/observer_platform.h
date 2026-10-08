#pragma once

#include <cstdint>

namespace rodakos_test::frontend_observer {
// Values come from the complete frontend's retirement host, never a second task registry.
void Tick(unsigned core, void* current_handle);
unsigned RegisteredHooks(unsigned core);
unsigned CurrentHandleReads();
void* LastTickHandle();
void FailNextCaptureTry();
unsigned RejectedCaptureTries();
}
