#pragma once

#include <cstddef>
#include <string>

namespace rodakos_test::wake_frontend {
void Reset();
void SetCreatable(bool aec, bool agc);
void SetAecFails(bool fails);
void SetAecFrame(int samples);
std::string AecFormat();
int AgcGainDb();
size_t LiveAec();
size_t LiveAgc();
size_t AecFrames();
size_t AgcBlocks();
}  // namespace rodakos_test::wake_frontend
