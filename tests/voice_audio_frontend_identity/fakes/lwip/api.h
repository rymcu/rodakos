#pragma once

// Host builds have no lwIP; the frontend only reserves its per-thread semaphore.
namespace rodakos_test {
void NoteNetconnThreadInit();
}

inline void netconn_thread_init() { rodakos_test::NoteNetconnThreadInit(); }
