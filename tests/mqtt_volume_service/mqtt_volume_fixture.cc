#include "service_fixture.h"
#include <iostream>

int main(int argc, char** argv) {
    try {
        mqtt_host::Fixture fixture;
        std::string device_key = "test-device";
        bool open_codec = false;
        bool fail_write = false;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option.rfind("--device-key=", 0) == 0) device_key = option.substr(13);
            else if (option == "--codec=open") open_codec = true;
            else if (option == "--codec=closed") open_codec = false;
            else if (option == "--fail-write") fail_write = true;
            else if (option == "--fail-light-refresh") fake_light::fail_refresh = true;
            else if (option.rfind("--fail-light-pixel=", 0) == 0)
                fake_light::fail_pixel_call = std::stoul(option.substr(19));
            else if (option == "--light=missing") fake_light::unavailable = true;
            else throw std::runtime_error("unknown fixture argument");
        }
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
        if (open_codec && !fixture.output.OpenForOwner("fixture", 16000, 1, 16))
            throw std::runtime_error("host codec open failed");
        fake_codec::fail_volume_write = fail_write;
        fixture.Start();
        size_t seen = 0;
        size_t processed = 0;
        std::string latest_shadow_report;
        std::string line;
        while (std::getline(std::cin, line)) {
            fixture.Send(line, true);
            fixture.Barrier();
            esp_mqtt_event_t flush;
            flush.event_id = MQTT_USER_EVENT;
            mqtt_host::Deliver(flush);
            const auto publications = mqtt_host::Publications();
            for (; seen < publications.size(); ++seen) {
                if (publications[seen].topic == prefix + "effects/receipt")
                    std::cout << publications[seen].payload << std::endl;
                if (publications[seen].topic == prefix + "shadow/report")
                    latest_shadow_report = publications[seen].payload;
            }
#ifdef LIGHT_EFFECT_FIXTURE
            constexpr const char* fixture_name = "mqtt-light";
#else
            constexpr const char* fixture_name = "mqtt-volume";
#endif
            rodakos::LightState light;
            fixture.lights.GetLight(0, light);
            std::cerr << "{\"fixture\":\"" << fixture_name << "\",\"processed\":" << ++processed
                      << ",\"volume\":" << fixture.output.volume()
                      << ",\"codecWrites\":" << fake_codec::volume_writes
                      << ",\"lightWrites\":" << fake_light::refresh_calls
                      << ",\"lightPixelWrites\":" << fake_light::pixel_calls
                      << ",\"light\":{\"id\":\"" << light.id << "\",\"enabled\":"
                      << (light.enabled ? "true" : "false") << ",\"brightness\":"
                      << unsigned(light.brightness_percent) << ",\"color\":{\"r\":"
                      << unsigned(light.color.red) << ",\"g\":" << unsigned(light.color.green)
                      << ",\"b\":" << unsigned(light.color.blue) << "},\"configurationRevision\":"
                      << light.configuration_revision << ",\"available\":"
                      << (light.available ? "true" : "false") << "}";
            auto escaped_report = mqtt_host::Json(cJSON_CreateString(latest_shadow_report.c_str()), cJSON_Delete);
            char* encoded_report = cJSON_PrintUnformatted(escaped_report.get());
            std::cerr << ",\"shadowReport\":" << (encoded_report ? encoded_report : "null") << "}" << std::endl;
            cJSON_free(encoded_report);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
