#pragma once
namespace rodakos {
class AudioOutputService {
public:
    bool IsOpen() { return open; }
    bool open = false;
};
}  // namespace rodakos
