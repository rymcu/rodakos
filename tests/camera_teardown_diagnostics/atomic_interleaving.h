#pragma once

#include <stdint.h>

extern "C" bool rodak_test_compare_exchange(uint32_t* address, uint32_t* expected,
    uint32_t desired, bool weak, int success_order, int failure_order);
extern "C" uint32_t rodak_test_load(const uint32_t* address, int order);

#define __atomic_compare_exchange_n rodak_test_compare_exchange
#define __atomic_load_n rodak_test_load
