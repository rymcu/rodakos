#pragma once
#include <cstddef>
#include <string>
namespace rodakos {
struct WebFileSystemServiceState {
    bool running = false;
    bool busy = false;
    std::string url;
    std::string token;
    std::string message;
    std::string last_file;
};
class WebFileSystemService {
public:
    WebFileSystemServiceState GetState() const { return state; }
    WebFileSystemServiceState state;
};
}  // namespace rodakos
