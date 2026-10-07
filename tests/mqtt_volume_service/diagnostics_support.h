#pragma once

#include <string>
#include <vector>

namespace mqtt_host {
struct DiagnosticLog {
    char level;
    std::string message;
};
void ResetDiagnosticLogs();
std::vector<DiagnosticLog> DiagnosticLogs();
}
