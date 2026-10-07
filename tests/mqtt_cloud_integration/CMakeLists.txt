cmake_minimum_required(VERSION 3.20)
project(rodakos_mqtt_cloud_integration LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
find_library(MBEDX509_LIBRARY mbedx509 REQUIRED)
set(MQTT "${CMAKE_CURRENT_SOURCE_DIR}/../mqtt_volume_service")
set(TRUST "${CMAKE_CURRENT_SOURCE_DIR}/../server_trust")
set(APP_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/../app_model")
set(MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../main")
set(RODAK_MQTT_SOURCE_ROOT "${MAIN}" CACHE PATH "Complete MQTT source/header snapshot root")
set(RODAK_CLOUD_SOURCE "${MAIN}/phone_os/device_cloud_config.cc" CACHE FILEPATH
    "Complete cloud translation unit for source-negative controls")
add_executable(mqtt_cloud_integration_tests
    mqtt_cloud_integration_test.cc
    "${MQTT}/diagnostic_test_main.cc"
    "${MQTT}/host_runtime.cc"
    "${MQTT}/diagnostics_support.cc"
    "${TRUST}/host_runtime.cc"
    ../light_service/host_driver.cc
    "${APP_MODEL}/third_party/cjson/cJSON.c"
    "${RODAK_MQTT_SOURCE_ROOT}/phone_os/unified_mqtt_service.cc"
    "${RODAK_CLOUD_SOURCE}"
    "${MAIN}/phone_os/server_trust.cc"
    "${MAIN}/phone_os/server_trust_route.cc"
    "${MAIN}/phone_os/server_trust_transport.cc"
    "${MAIN}/phone_os/server_trust_discovery.cc"
    "${MAIN}/phone_os/device_pairing_protocol.cc"
    "${MAIN}/phone_os/serial_provisioning_protocol.cc"
    "${MAIN}/phone_os/serial_provisioning_binding.cc"
    "${MAIN}/phone_os/mqtt_command_ledger.cc"
    "${MAIN}/phone_os/mqtt_volume_effect.cc"
    "${MAIN}/phone_os/mqtt_light_effect.cc"
    "${MAIN}/phone_os/light_service.cc"
    "${MAIN}/rodakos_adapters/board_device_adapter.cc"
    "${RODAK_MQTT_SOURCE_ROOT}/phone_os/mqtt_credential_refresh_policy.cc"
    "${MAIN}/phone_os/audio_output_service.cc"
    "${MAIN}/phone_os/realtime_voice_contract.cc"
    "${MAIN}/phone_os/voice_identity.cc"
    "${MAIN}/rodakos_adapters/audio_codec_output.cc")
target_include_directories(mqtt_cloud_integration_tests BEFORE PRIVATE
    fakes "${MQTT}/fakes" "${TRUST}/fakes" "${MQTT}"
    ../light_service/fakes ../light_service
    "${APP_MODEL}/fakes" "${APP_MODEL}" "${APP_MODEL}/third_party/cjson"
    "${RODAK_MQTT_SOURCE_ROOT}" "${MAIN}" "${MAIN}/../components/rodak_ota_state/include")
target_compile_features(mqtt_cloud_integration_tests PRIVATE cxx_std_20)
target_compile_definitions(mqtt_cloud_integration_tests PRIVATE
    RODAK_MQTT_REAL_CLOUD=1 TEST_CA_PATH="${TRUST}/test-ca.pem")
target_compile_options(mqtt_cloud_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(mqtt_cloud_integration_tests PRIVATE
    Threads::Threads "${MBEDCRYPTO_LIBRARY}" "${MBEDX509_LIBRARY}")
add_test(NAME mqtt_cloud_integration COMMAND mqtt_cloud_integration_tests)
set_tests_properties(mqtt_cloud_integration PROPERTIES TIMEOUT 60)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
    "{\n  \"mode\": \"complete-mqtt-and-cloud-translation-units\"")
foreach(source IN ITEMS "${RODAK_MQTT_SOURCE_ROOT}/phone_os/unified_mqtt_service.cc"
    "${RODAK_MQTT_SOURCE_ROOT}/phone_os/unified_mqtt_service.h"
    "${RODAK_MQTT_SOURCE_ROOT}/phone_os/mqtt_credential_refresh_policy.cc"
    "${RODAK_MQTT_SOURCE_ROOT}/phone_os/mqtt_credential_refresh_policy.h"
    "${RODAK_CLOUD_SOURCE}" "${MAIN}/phone_os/device_cloud_config.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(SHA256 "${source}" source_sha)
    get_filename_component(source_name "${source}" NAME)
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
        ",\n  \"${source_name}\": \"${source_sha}\"")
endforeach()
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json" "\n}\n")
get_filename_component(selected_cloud "${RODAK_CLOUD_SOURCE}" ABSOLUTE)
get_filename_component(production_cloud "${MAIN}/phone_os/device_cloud_config.cc" ABSOLUTE)
get_filename_component(selected_mqtt "${RODAK_MQTT_SOURCE_ROOT}" ABSOLUTE)
get_filename_component(production_mqtt "${MAIN}" ABSOLUTE)
if(selected_cloud STREQUAL production_cloud AND selected_mqtt STREQUAL production_mqtt)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME mqtt_cloud_fence_negative_controls
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
        --output "${CMAKE_CURRENT_BINARY_DIR}/negative-controls"
        --c-flags=${CMAKE_C_FLAGS} --cxx-flags=${CMAKE_CXX_FLAGS}
        --link-flags=${CMAKE_EXE_LINKER_FLAGS})
    set_tests_properties(mqtt_cloud_fence_negative_controls PROPERTIES TIMEOUT 300)
endif()
