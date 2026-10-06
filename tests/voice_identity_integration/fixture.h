#pragma once
#include "service_fixture.h"
#include "phone_os/voice_wake_service.h"
#include "settings.h"
#include <deque>
#include <mutex>

namespace identity_host {
class Runtime final : public rodakos::VoiceWakeRuntime {
public:
    bool Init() override { return true; }
    void Deinit() override { StopListening(); }
    bool StartListening(std::function<void(const std::string&)>) override {
        std::lock_guard<std::mutex> lock(mutex_);
        listening_ = true;
        return true;
    }
    void StopListening() override { std::lock_guard<std::mutex> lock(mutex_); listening_ = false; }
    bool IsListening() const override { std::lock_guard<std::mutex> lock(mutex_); return listening_; }
    bool IsAvailable() const override { return true; }
    bool ConfigureWakeWord(const rodakos::VoiceIdentityConfig& config) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++configure_calls_;
        bool accepted = true;
        if (!results_.empty()) { accepted = results_.front(); results_.pop_front(); }
        if (accepted) active_ = config;
        return accepted;
    }
    const char* name() const override { return "host-runtime"; }
    const char* last_error() const override { return "host runtime configuration rejected"; }
    void FailCandidateAndRollback() {
        std::lock_guard<std::mutex> lock(mutex_);
        results_ = {false, false};
    }
    unsigned ConfigureCalls() const { std::lock_guard<std::mutex> lock(mutex_); return configure_calls_; }
    rodakos::VoiceIdentityConfig Active() const { std::lock_guard<std::mutex> lock(mutex_); return active_; }
private:
    mutable std::mutex mutex_;
    bool listening_ = false;
    unsigned configure_calls_ = 0;
    std::deque<bool> results_;
    rodakos::VoiceIdentityConfig active_ = rodakos::DefaultVoiceIdentityConfig();
};

class Fixture {
public:
    mqtt_host::Fixture mqtt;
    Runtime runtime;
    rodakos::VoiceAssistantService assistant;
    std::mutex clock_mutex;
    rodakos::VoiceIdentityClockSnapshot clock{true, 1800000000000LL, 1000};
    rodakos::VoiceWakeService wake{assistant, runtime, [this] {
        std::lock_guard<std::mutex> lock(clock_mutex); return clock;
    }};

    explicit Fixture(const std::string& device_key = "test-device") {
        wake_host::ResetStore();
        if (device_key.empty() || device_key.find_first_of("/+#") != std::string::npos)
            throw std::runtime_error("invalid identity fixture device key");
        auto config = mqtt_host::Config();
        const std::string old_prefix = "devices/test-device/";
        const std::string prefix = "devices/" + device_key + "/";
        config.mqtt_device_key = config.mqtt_username = device_key;
        for (auto member : {&rodakos::DeviceCloudConfig::mqtt_topic_shadow_desired,
                &rodakos::DeviceCloudConfig::mqtt_topic_shadow_report,
                &rodakos::DeviceCloudConfig::mqtt_topic_telemetry,
                &rodakos::DeviceCloudConfig::mqtt_topic_ota_notify,
                &rodakos::DeviceCloudConfig::mqtt_topic_ota_progress,
                &rodakos::DeviceCloudConfig::mqtt_topic_commands,
                &rodakos::DeviceCloudConfig::mqtt_topic_pc_status})
            (config.*member).replace(0, old_prefix.size(), prefix);
        mqtt_host::SetConfig(config);
        RODAK_CHECK(wake.Start());
        mqtt.service.SetVoiceWakeService(&wake);
        mqtt.Start();
        WaitStatus("applied");
    }
    ~Fixture() {
        mqtt.service.Stop();
        wake.Deinit();
        mqtt_host::JoinWorkers();
    }
    void Desired(const std::string& topic, const std::string& payload) {
        RODAK_CHECK_EQ(topic, mqtt_host::Config().mqtt_topic_shadow_desired);
        mqtt_host::Message(topic, payload, payload.size() > 1);
        mqtt.Barrier();
    }
    void Identity(const std::string& body) {
        Desired(mqtt_host::Config().mqtt_topic_shadow_desired,
            "{\"desired\":{\"voice_identity\":" + body + "}}");
    }
    void SetClock(bool valid, int64_t unix_ms, int64_t monotonic_ms) {
        std::lock_guard<std::mutex> lock(clock_mutex);
        clock = {valid, unix_ms, monotonic_ms};
    }
    std::vector<mqtt_host::Publication> Reports() const {
        std::vector<mqtt_host::Publication> reports;
        for (const auto& item : mqtt_host::Publications())
            if (item.topic == mqtt_host::Config().mqtt_topic_shadow_report) reports.push_back(item);
        return reports;
    }
    mqtt_host::Json Report() const {
        const auto reports = Reports();
        RODAK_CHECK_FALSE(reports.empty());
        return mqtt_host::Parse(reports.back().payload);
    }
    void WaitStatus(const std::string& expected) const {
        RODAK_CHECK(mqtt_host::WaitUntil([&] {
            const auto reports = Reports();
            if (reports.empty()) return false;
            const auto report = mqtt_host::Parse(reports.back().payload);
            const auto* status = mqtt_host::Get(mqtt_host::Get(report.get(), "voice_identity"), "status");
            return cJSON_IsString(status) && expected == status->valuestring;
        }));
    }
};

inline std::string Request(unsigned revision, const char* name, bool temporary = false,
                           int64_t expiry = 1800000001000LL) {
    return "{\"name\":\"" + std::string(name) + "\",\"wakeWord\":\"你好达克\","
        "\"wakeCommand\":\"ni hao da ke\",\"revision\":" + std::to_string(revision) +
        ",\"mode\":\"" + (temporary ? "temporary" : "persistent") + "\"" +
        (temporary ? ",\"expiresAtMs\":" + std::to_string(expiry) : "") + "}";
}
}
