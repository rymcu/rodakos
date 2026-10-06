#pragma once
#include <cstdlib>
namespace photo_test { inline int decoded_buffers = 0; }
inline void jpeg_free_align(void* pointer) {
    if (pointer != nullptr) { --photo_test::decoded_buffers; std::free(pointer); }
}
