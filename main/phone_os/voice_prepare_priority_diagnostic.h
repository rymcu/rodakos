#pragma once

#ifdef RODAKOS_RELEASE_TESTS
namespace rodakos {

// Consumes a completed diagnostic record; never waits for an active preparation.
void PrintVoicePreparePrioritySnapshot();

}  // namespace rodakos
#endif
