#pragma once
#include <cstddef>
namespace rodakos {
class MusicPlayerService {
public:
    size_t track_count() { return tracks; }
    size_t tracks = 0;
};
}  // namespace rodakos
