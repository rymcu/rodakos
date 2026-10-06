#include "service_fixture.h"
#include <iostream>

int main(int argc, char** argv) {
    try {
        mqtt_host::Fixture fixture;
        std::string device_key = "test-device";
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option.rfind("--device-key=", 0) == 0) device_key = option.substr(13);
            else throw std::runtime_error("unknown command fixture argument");
        }
        if (device_key.empty() || device_key.find_first_of("/+#") != std::string::npos)
            throw std::runtime_error("invalid command fixture device key");
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
                            &rodakos::DeviceCloudConfig::mqtt_topic_pc_status}) {
            (config.*member).replace(0, old_prefix.size(), prefix);
        }
        mqtt_host::SetConfig(config);
        fixture.Start();
        size_t processed = 0;
        std::string line;
        while (std::getline(std::cin, line)) {
            const auto input = mqtt_host::Parse(line);
            const auto* topic_json = mqtt_host::Get(input.get(), "topic");
            const auto* payload_json = mqtt_host::Get(input.get(), "payload");
            if (!cJSON_IsString(topic_json) || !cJSON_IsString(payload_json))
                throw std::runtime_error("expected {topic:string,payload:string} transport input");
            const std::string topic = topic_json->valuestring;
            const std::string command_prefix = prefix + "commands/";
            if (topic.rfind(command_prefix, 0) != 0)
                throw std::runtime_error("command fixture topic belongs to another device");
            const std::string command_no = topic.substr(command_prefix.size());
            if (command_no.empty() || command_no == "host-barrier" ||
                command_no.find_first_of("/+#") != std::string::npos)
                throw std::runtime_error("invalid command fixture command number");
            const size_t previous = mqtt_host::Publications().size();
            const std::string payload = payload_json->valuestring;
            mqtt_host::Message(topic, payload, payload.size() > 1);
            fixture.Barrier();
            RODAK_CHECK(mqtt_host::WaitUntil([&]() {
                const auto items = mqtt_host::Publications();
                for (size_t index = previous; index < items.size(); ++index)
                    if (items[index].topic == topic + "/ack") return true;
                return false;
            }));
            const auto publications = mqtt_host::Publications();
            const mqtt_host::Publication* acknowledgement = nullptr;
            for (size_t index = previous; index < publications.size(); ++index) {
                if (publications[index].topic != topic + "/ack") continue;
                if (acknowledgement != nullptr)
                    throw std::runtime_error("multiple command acknowledgements for one input");
                acknowledgement = &publications[index];
            }
            if (acknowledgement == nullptr)
                throw std::runtime_error("production command handler emitted no acknowledgement");
            auto output = mqtt_host::Json(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddNumberToObject(output.get(), "processed", ++processed);
            cJSON_AddStringToObject(output.get(), "topic", acknowledgement->topic.c_str());
            // JSON 包装只保护逐行传输；ACK 正文必须来自生产 MQTT publication。
            cJSON_AddStringToObject(output.get(), "payload", acknowledgement->payload.c_str());
            char* encoded = cJSON_PrintUnformatted(output.get());
            if (encoded == nullptr) throw std::runtime_error("failed to encode captured publication");
            std::cout << encoded << std::endl;
            cJSON_free(encoded);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
