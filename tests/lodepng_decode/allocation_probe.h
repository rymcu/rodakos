#ifndef RODAKOS_LODEPNG_ALLOCATION_PROBE_H
#define RODAKOS_LODEPNG_ALLOCATION_PROBE_H

#include <stddef.h>

int verify_rgba8_allocation_recovery(const unsigned char * png, size_t png_size,
                                    const unsigned char * expected, size_t expected_size,
                                    unsigned width, unsigned height);

#endif
