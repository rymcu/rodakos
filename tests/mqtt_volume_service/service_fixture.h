#pragma once
#include "host_runtime.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/ota_update_service.h"
#include "phone_os/unified_mqtt_service.h"
#include "test_framework.h"
#include <esp_codec_dev.h>
#include <cJSON.h>
#include <memory>

namespace mqtt_host {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
inline Json Parse(const std::string& value) { return Json(cJSON_Parse(value.c_str()), cJSON_Delete); }
inline const cJSON* Get(const cJSON* root, const char* key) { return cJSON_GetObjectItemCaseSensitive(root, key); }
inline std::string Request(const std::string& id = "effect-1", int volume = 30, unsigned version = 1,
                            const std::string& device_key = "test-device") {
    return "{\"deviceKey\":\"" + device_key + "\",\"version\":" + std::to_string(version) +
        ",\"shadowVersion\":" + std::to_string(version) + ",\"desired\":{\"volume\":" +
        std::to_string(volume) + "},\"_meta\":{\"rodak/deviceEffect\":{"
        "\"schema\":\"rodak.mqtt-volume-effect.v1\",\"effectId\":\"" + id +
        "\",\"parametersHash\":\"" + std::string(64, 'a') + "\",\"dispatchId\":\"dispatch-" +
        id + "\",\"shadowVersion\":" + std::to_string(version) +
        ",\"operation\":\"volume.set\",\"requested\":{\"volume\":" + std::to_string(volume) + "}}}}";
}
struct ResetGuard { ResetGuard() { Reset(); fake_codec::Reset(); } };
struct Fixture {
    ~Fixture() {
        PauseDequeue(false);
        HoldUserEvents(false);
        service.Stop();
        JoinWorkers();
    }
    void Start() {
        RODAK_CHECK(service.Start());
        RODAK_CHECK(WaitUntil([&]() { return service.IsConnected(); }));
        RODAK_CHECK(WaitUntil([]() { return !Publications().empty(); }));
    }
    void Send(const std::string& payload, bool fragmented = false) {
        Message(Config().mqtt_topic_shadow_desired, payload, fragmented);
    }
    std::string Receipt(size_t index = 0) {
        RODAK_CHECK(WaitUntil([=]() { return ReceiptCount() > index; }));
        size_t found = 0;
        for (const auto& item : Publications()) {
            if (item.topic.find("/effects/receipt") != std::string::npos) {
                if (found++ == index) return item.payload;
            }
        }
        return {};
    }
    void Barrier() {
        const size_t previous = Publications().size();
        Message("devices/" + Config().mqtt_device_key + "/commands/host-barrier", "ping");
        auto sent_at = std::chrono::steady_clock::now();
        RODAK_CHECK(WaitUntil([&]() {
            const auto items = Publications();
            for (size_t i = previous; i < items.size(); ++i)
                if (items[i].topic.find("/commands/host-barrier/ack") != std::string::npos) return true;
            // 凭据切换可合法丢弃旧 epoch 的 marker；新的 marker 仍须经真实队列。
            if (std::chrono::steady_clock::now() - sent_at > std::chrono::milliseconds(50)) {
                Message("devices/" + Config().mqtt_device_key + "/commands/host-barrier", "ping");
                sent_at = std::chrono::steady_clock::now();
            }
            return false;
        }));
    }
    ResetGuard reset;
    rodakos::AudioOutputService output;
    rodakos::DeviceCloudConfigService config_service;
    rodakos::OtaUpdateService ota;
    rodakos::UnifiedMqttService service{config_service, ota, &output};
};
}
