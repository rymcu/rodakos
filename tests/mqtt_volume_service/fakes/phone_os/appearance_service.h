#pragma once
#include <functional>
#include <string>
namespace rodakos {
class AppearanceService {
public:
    void SetStatePublisher(std::function<void()>) {}
    bool IsBusy() const { return false; }
    void OnNetworkReady() {}
    void ApplyDesiredJson(const char*) {}
    std::string ReportedJson() const { return "{}"; }
};
}
