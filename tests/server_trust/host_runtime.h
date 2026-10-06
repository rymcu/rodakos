#pragma once
#include "phone_os/server_trust.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace trust_test {
struct Reply {
    int status = 200;
    std::string body;
    bool tls_ok = true;
};
struct Request {
    std::string url;
    std::string body;
    std::string certificate;
    std::string common_name;
    bool public_bundle = false;
    bool skip_name = false;
    bool redirects_disabled = false;
    std::string host_header;
    bool tls_verified = false;
};
struct Discovery {
    std::string hostname;
    std::string server_id;
    std::string version = "1";
    uint16_t port = 9443;
    std::vector<std::string> addresses = {"192.168.137.1"};
};
extern std::map<std::string, std::string> strings;
extern std::map<std::string, bool> booleans;
extern std::map<std::string, int> integers;
extern std::map<std::string, Reply> replies;
extern std::vector<Request> requests;
extern std::vector<Discovery> discoveries;
extern std::string read_error_key;
extern std::string write_error_key;
extern size_t authority_write_capacity;
extern unsigned discovery_calls;
extern std::function<void(const std::string&)> on_http_open;
extern std::function<void()> on_discovery;
void Reset();
void AdvanceTimeMs(int64_t milliseconds);
rodakos::ServerTrust TestTrust();
std::string BootstrapUrl(int port = 9443);
void SeedBoundLegacy();
void RespondBound(int port = 9443, const std::string& transport = "mqtts");
void RespondBoundAt(const std::string& address, int port = 9443,
                    const std::string& transport = "mqtts");
}  // namespace trust_test
