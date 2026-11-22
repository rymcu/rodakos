#pragma once
#include <cstdint>
namespace rodakos {
class FileService {
public:
    struct Capacity {
        uint64_t total_bytes = 0;
        uint64_t free_bytes = 0;
    };
    bool IsMounted() const { return mounted; }
    bool Init() { return mounted; }
    bool GetCapacity(Capacity& out) const { out = capacity; return mounted; }
    bool mounted = false;
    Capacity capacity;
};
}  // namespace rodakos
