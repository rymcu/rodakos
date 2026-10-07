#pragma once
#include <functional>
#include <mutex>
#include <string>
namespace rodakos {
class OtaUpdateService {
public:
    void SetProgressPublisher(std::function<bool(const std::string&, bool)> value) {
        std::lock_guard<std::mutex> lock(mutex_);
        publisher = std::move(value);
    }
    void HandleNotification(const std::string&) {}
    void OnNetworkReady() {}
    bool EmitProgress(const std::string& payload, bool reliable) {
        std::function<bool(const std::string&, bool)> current;
        { std::lock_guard<std::mutex> lock(mutex_); current = publisher; }
        return current && current(payload, reliable);
    }
private:
    std::mutex mutex_;
    std::function<bool(const std::string&, bool)> publisher;
};
}
