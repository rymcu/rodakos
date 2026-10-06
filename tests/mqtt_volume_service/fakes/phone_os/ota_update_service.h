#pragma once
#include <functional>
#include <string>
namespace rodakos {
class OtaUpdateService {
public:
    void SetProgressPublisher(std::function<bool(const std::string&, bool)> value) { publisher = std::move(value); }
    void HandleNotification(const std::string&) {}
    void OnNetworkReady() {}
private:
    std::function<bool(const std::string&, bool)> publisher;
};
}
