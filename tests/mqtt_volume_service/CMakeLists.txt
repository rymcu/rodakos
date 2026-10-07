cmake_minimum_required(VERSION 3.16)
project(rodakos_mqtt_volume_service_tests LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
set(APP_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/../app_model")
set(MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../main")
set(RODAK_MQTT_SERVICE_SOURCE "${MAIN}/phone_os/unified_mqtt_service.cc" CACHE FILEPATH
    "Complete MQTT service translation unit for baseline and negative host controls")
set(RODAK_MQTT_POLICY_SOURCE "${MAIN}/phone_os/mqtt_credential_refresh_policy.cc" CACHE FILEPATH
    "Credential refresh policy source paired with the reviewed complete service")
set(RODAK_MQTT_INCLUDE_ROOT "" CACHE PATH
    "Optional complete baseline header root containing phone_os MQTT service/policy headers")
set(RODAK_MQTT_SERVICE_HEADER "${MAIN}/phone_os/unified_mqtt_service.h")
if(RODAK_MQTT_INCLUDE_ROOT)
    set(RODAK_MQTT_SERVICE_HEADER "${RODAK_MQTT_INCLUDE_ROOT}/phone_os/unified_mqtt_service.h")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${RODAK_MQTT_SERVICE_SOURCE}" "${RODAK_MQTT_SERVICE_HEADER}")
file(SHA256 "${RODAK_MQTT_SERVICE_SOURCE}" service_sha)
file(SHA256 "${RODAK_MQTT_SERVICE_HEADER}" header_sha)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
    "{\n  \"mode\": \"complete-service-translation-unit\",\n  \"unified_mqtt_service.cc\": \"${service_sha}\",\n  \"unified_mqtt_service.h\": \"${header_sha}\"\n}\n")

add_library(mqtt_volume_host STATIC
    host_runtime.cc
    diagnostics_support.cc
    ../light_service/host_driver.cc
    "${APP_MODEL}/third_party/cjson/cJSON.c"
    "${RODAK_MQTT_SERVICE_SOURCE}"
    "${MAIN}/phone_os/server_trust_route.cc"
    "${MAIN}/phone_os/mqtt_command_ledger.cc"
    "${MAIN}/phone_os/mqtt_volume_effect.cc"
    "${MAIN}/phone_os/mqtt_light_effect.cc"
    "${MAIN}/phone_os/light_service.cc"
    "${MAIN}/rodakos_adapters/board_device_adapter.cc"
    "${RODAK_MQTT_POLICY_SOURCE}"
    "${MAIN}/phone_os/audio_output_service.cc"
    "${MAIN}/phone_os/realtime_voice_contract.cc"
    "${MAIN}/phone_os/voice_identity.cc"
    "${MAIN}/rodakos_adapters/audio_codec_output.cc"
)
target_include_directories(mqtt_volume_host BEFORE PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RODAK_MQTT_INCLUDE_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../light_service/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}/../light_service"
    "${APP_MODEL}/fakes"
    "${APP_MODEL}"
    "${APP_MODEL}/third_party/cjson"
    "${MAIN}"
    "${MAIN}/../components/rodak_ota_state/include"
)
target_compile_features(mqtt_volume_host PUBLIC cxx_std_20)
target_link_libraries(mqtt_volume_host PUBLIC Threads::Threads ${MBEDCRYPTO_LIBRARY})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mqtt_volume_host PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(rodakos_mqtt_volume_service_tests mqtt_volume_service_test.cc mqtt_light_service_test.cc "${APP_MODEL}/test_main.cc")
target_link_libraries(rodakos_mqtt_volume_service_tests PRIVATE mqtt_volume_host)
add_test(NAME rodakos_mqtt_volume_service COMMAND rodakos_mqtt_volume_service_tests)
set_tests_properties(rodakos_mqtt_volume_service PROPERTIES TIMEOUT 40)

add_executable(rodakos_mqtt_volume_fixture mqtt_volume_fixture.cc)
target_link_libraries(rodakos_mqtt_volume_fixture PRIVATE mqtt_volume_host)

add_executable(rodakos_mqtt_light_fixture mqtt_volume_fixture.cc)
target_compile_definitions(rodakos_mqtt_light_fixture PRIVATE LIGHT_EFFECT_FIXTURE=1)
target_link_libraries(rodakos_mqtt_light_fixture PRIVATE mqtt_volume_host)

add_executable(rodakos_mqtt_command_fixture mqtt_command_fixture.cc)
target_link_libraries(rodakos_mqtt_command_fixture PRIVATE mqtt_volume_host)

add_executable(rodakos_mqtt_command_service_tests mqtt_command_service_test.cc mqtt_command_epoch_test.cc mqtt_stream_lifecycle_test.cc mqtt_command_ledger_test.cc mqtt_command_replay_test.cc "${APP_MODEL}/test_main.cc")
target_link_libraries(rodakos_mqtt_command_service_tests PRIVATE mqtt_volume_host)
add_test(NAME rodakos_mqtt_command_service COMMAND rodakos_mqtt_command_service_tests)
set_tests_properties(rodakos_mqtt_command_service PROPERTIES TIMEOUT 40)

add_executable(rodakos_mqtt_voice_identity_tests mqtt_voice_identity_test.cc "${APP_MODEL}/test_main.cc")
target_link_libraries(rodakos_mqtt_voice_identity_tests PRIVATE mqtt_volume_host)
add_test(NAME rodakos_mqtt_voice_identity COMMAND rodakos_mqtt_voice_identity_tests)
set_tests_properties(rodakos_mqtt_voice_identity PROPERTIES TIMEOUT 40)

add_executable(rodakos_mqtt_credential_replacement_tests
    mqtt_credential_replacement_test.cc diagnostic_test_main.cc)
target_link_libraries(rodakos_mqtt_credential_replacement_tests PRIVATE mqtt_volume_host)
add_test(NAME rodakos_mqtt_credential_replacement COMMAND rodakos_mqtt_credential_replacement_tests)
set_tests_properties(rodakos_mqtt_credential_replacement PROPERTIES TIMEOUT 90)

add_executable(rodakos_mqtt_queue_diagnostics_tests
    mqtt_queue_diagnostics_test.cc allocation_probe.cc diagnostic_test_main.cc)
target_link_libraries(rodakos_mqtt_queue_diagnostics_tests PRIVATE mqtt_volume_host)
target_link_options(rodakos_mqtt_queue_diagnostics_tests PRIVATE
    "-Wl,--wrap=_ZdlPv" "-Wl,--wrap=_ZdlPvm")
add_test(NAME rodakos_mqtt_queue_diagnostics COMMAND rodakos_mqtt_queue_diagnostics_tests)
set_tests_properties(rodakos_mqtt_queue_diagnostics PROPERTIES TIMEOUT 40)
get_filename_component(selected_service "${RODAK_MQTT_SERVICE_SOURCE}" ABSOLUTE)
get_filename_component(production_service "${MAIN}/phone_os/unified_mqtt_service.cc" ABSOLUTE)
if(selected_service STREQUAL production_service)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME rodakos_mqtt_diagnostic_negative_controls
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
        --output "${CMAKE_CURRENT_BINARY_DIR}/negative-controls"
        --c-flags=${CMAKE_C_FLAGS} --cxx-flags=${CMAKE_CXX_FLAGS}
        --link-flags=${CMAKE_EXE_LINKER_FLAGS})
    set_tests_properties(rodakos_mqtt_diagnostic_negative_controls PROPERTIES TIMEOUT 480)
    add_test(NAME rodakos_mqtt_credential_negative_controls
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_credential_negative.py"
        --output "${CMAKE_CURRENT_BINARY_DIR}/replacement-negative-controls"
        --c-flags=${CMAKE_C_FLAGS} --cxx-flags=${CMAKE_CXX_FLAGS}
        --link-flags=${CMAKE_EXE_LINKER_FLAGS})
    set_tests_properties(rodakos_mqtt_credential_negative_controls PROPERTIES TIMEOUT 480)
endif()
