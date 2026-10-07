#include "diagnostics_support.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace mqtt_host {
namespace {
struct LogSlot { char level; std::array<char, 768> text; };
std::array<LogSlot, 64> logs;
std::mutex log_mutex;
size_t log_count = 0;
bool overflow = false;
}

void CaptureLog(char level, const char* tag, const char* format, ...) {
    if (std::strcmp(tag, "UnifiedMqtt") != 0 ||
        (std::strncmp(format, "MQTT message dropped:", 21) != 0 &&
         std::strncmp(format, "Dropping command output:", 24) != 0)) return;
    std::lock_guard<std::mutex> lock(log_mutex);
    if (log_count == logs.size()) { overflow = true; return; }
    auto& entry = logs[log_count++];
    entry.level = level;
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(entry.text.data(), entry.text.size(), format, args);
    va_end(args);
    if (length < 0 || static_cast<size_t>(length) >= entry.text.size()) overflow = true;
}

void ResetDiagnosticLogs() {
    std::lock_guard<std::mutex> lock(log_mutex);
    log_count = 0;
    overflow = false;
}

std::vector<DiagnosticLog> DiagnosticLogs() {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (overflow) throw std::runtime_error("MQTT diagnostic capture overflow");
    std::vector<DiagnosticLog> result;
    for (size_t index = 0; index < log_count; ++index)
        result.push_back({logs[index].level, logs[index].text.data()});
    return result;
}
}
