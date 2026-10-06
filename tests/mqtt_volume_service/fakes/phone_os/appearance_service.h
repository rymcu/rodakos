#pragma once
#include <functional>
#include <string>
namespace rodakos {
class AppearanceService {
public:
    unsigned desired_calls = 0;
    void SetStatePublisher(std::function<void()>) {}
    bool IsBusy() const { return false; }
    void OnNetworkReady() {}
    void ApplyDesiredJson(const char*) { ++desired_calls; }
    std::string ReportedJson() const { return "{}"; }
};
}
