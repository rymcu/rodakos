#include "fixture.h"
#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    try {
        std::string device_key = "test-device";
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument.rfind("--device-key=", 0) != 0) throw std::runtime_error("invalid argument");
            device_key = argument.substr(13);
        }
        identity_host::Fixture fixture(device_key);
        size_t processed = 0;
        std::string line;
        while (std::getline(std::cin, line)) {
            const auto input = mqtt_host::Parse(line);
            const auto* operation = mqtt_host::Get(input.get(), "op");
            if (!cJSON_IsString(operation)) throw std::runtime_error("missing fixture operation");
            const std::string op = operation->valuestring;
            if (op == "desired") {
                const auto* topic = mqtt_host::Get(input.get(), "topic");
                const auto* payload = mqtt_host::Get(input.get(), "payload");
                if (!cJSON_IsString(topic) || !cJSON_IsString(payload))
                    throw std::runtime_error("desired requires raw topic and payload strings");
                fixture.Desired(topic->valuestring, payload->valuestring);
            } else if (op == "clock") {
                const auto* unix_ms = mqtt_host::Get(input.get(), "unixMs");
                const auto* monotonic_ms = mqtt_host::Get(input.get(), "monotonicMs");
                const auto valid = [](const cJSON* value) {
                    return cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
                        value->valuedouble >= 0 && value->valuedouble <= 9007199254740991.0 &&
                        std::floor(value->valuedouble) == value->valuedouble;
                };
                if (!valid(unix_ms) || !valid(monotonic_ms))
                    throw std::runtime_error("clock requires millisecond values");
                fixture.SetClock(true, static_cast<int64_t>(unix_ms->valuedouble),
                    static_cast<int64_t>(monotonic_ms->valuedouble));
            } else if (op == "fail-configuration-and-rollback") {
                fixture.runtime.FailCandidateAndRollback();
            } else if (op != "snapshot") {
                throw std::runtime_error("unknown fixture operation");
            }
            const auto* expected = mqtt_host::Get(input.get(), "waitStatus");
            if (cJSON_IsString(expected)) fixture.WaitStatus(expected->valuestring);
            const auto reports = fixture.Reports();
            if (reports.empty()) throw std::runtime_error("production service emitted no shadow report");
            const auto& publication = reports.back();
            auto output = mqtt_host::Json(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddNumberToObject(output.get(), "processed", ++processed);
            cJSON_AddStringToObject(output.get(), "topic", publication.topic.c_str());
            cJSON_AddStringToObject(output.get(), "payload", publication.payload.c_str());
            cJSON_AddNumberToObject(output.get(), "runtimeConfigurations", fixture.runtime.ConfigureCalls());
            char* encoded = cJSON_PrintUnformatted(output.get());
            if (encoded == nullptr) throw std::runtime_error("failed to encode transport wrapper");
            std::cout << encoded << std::endl;
            cJSON_free(encoded);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
