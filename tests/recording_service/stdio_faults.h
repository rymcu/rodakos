#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
namespace recording_host {
struct StdioFaults {
    bool initial_header = false;
    bool final_header = false;
    bool data_write = false;
    bool seek = false;
    bool flush = false;
    bool close = false;
    bool fdopen = false;
    bool remove = false;
    bool create = false;
    bool all_names_exist = false;
    unsigned library_read_error = 0;
    unsigned library_close_error = 0;
    std::function<void()> final_header_hook;
    std::function<void()> close_hook;
};
void SetStdioFaults(const StdioFaults& faults);
void ResetStdioFaults();
size_t OutstandingRecordingDescriptors();
std::vector<std::string> StdioEvents();
}
