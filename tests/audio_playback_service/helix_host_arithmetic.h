#pragma once
// Host arithmetic adapter for the managed decoder's architecture-specific intrinsics.
// All parsing, reservoir handling and decoding still compile from the pinned Helix source.
#define _ASSEMBLY_H
#include <stdint.h>
typedef long long Word64;
static inline int MULSHIFT32(int x, int y) { return (int)(((int64_t)x * y) >> 32); }
static inline int FASTABS(int x) { return x < 0 ? (int)(0U - (unsigned)x) : x; }
static inline int CLZ(int x) { return x == 0 ? 32 : __builtin_clz((unsigned)x); }
static inline Word64 MADD64(Word64 sum, int x, int y) { return (Word64)((uint64_t)sum + (uint64_t)((int64_t)x * y)); }
static inline Word64 SHL64(Word64 x, int n) { return (Word64)((uint64_t)x << n); }
static inline Word64 SAR64(Word64 x, int n) { return x >> n; }
