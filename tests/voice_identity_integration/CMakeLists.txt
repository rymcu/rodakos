cmake_minimum_required(VERSION 3.20)
project(rodakos_voice_identity_integration LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MQTT "${ROOT}/tests/mqtt_volume_service")
set(MAIN "${ROOT}/main")
set(WAKE "${ROOT}/tests/voice_wake_service")
set(RODAK_TASK_RETIREMENT_BUILD_TESTS OFF)
add_subdirectory("${ROOT}/tests/task_retirement" "${CMAKE_CURRENT_BINARY_DIR}/task-retirement")

add_library(voice_identity_integration_host STATIC
    "${MQTT}/host_runtime.cc"
    "${MQTT}/diagnostics_support.cc"
    "${ROOT}/tests/light_service/host_driver.cc"
    "${ROOT}/managed_components/espressif__cjson/cJSON/cJSON.c"
    "${MAIN}/phone_os/unified_mqtt_service.cc"
    "${MAIN}/phone_os/server_trust_route.cc"
    "${MAIN}/phone_os/voice_wake_service.cc"
    "${MAIN}/phone_os/voice_wake_settings.cc"
    "${MAIN}/phone_os/voice_identity.cc"
    "${MAIN}/phone_os/voice_identity_record.cc"
    "${MAIN}/phone_os/time_service.cc"
    "${MAIN}/phone_os/mqtt_command_ledger.cc"
    "${MAIN}/phone_os/mqtt_volume_effect.cc"
    "${MAIN}/phone_os/mqtt_light_effect.cc"
    "${MAIN}/phone_os/light_service.cc"
    "${MAIN}/phone_os/mqtt_credential_refresh_policy.cc"
    "${MAIN}/phone_os/audio_output_service.cc"
    "${MAIN}/phone_os/realtime_voice_contract.cc"
    "${MAIN}/rodakos_adapters/board_device_adapter.cc"
    "${MAIN}/rodakos_adapters/audio_codec_output.cc")
target_include_directories(voice_identity_integration_host BEFORE PUBLIC
    "${ROOT}/tests/task_retirement/fakes"
    "${WAKE}/fakes" "${MQTT}/fakes" "${MQTT}"
    "${ROOT}/tests/light_service/fakes" "${ROOT}/tests/light_service"
    "${ROOT}/tests/app_model/fakes" "${ROOT}/tests/app_model"
    "${ROOT}/managed_components/espressif__cjson/cJSON"
    "${MAIN}" "${ROOT}/components/rodak_ota_state/include")
target_compile_features(voice_identity_integration_host PUBLIC cxx_std_20)
target_compile_definitions(voice_identity_integration_host PUBLIC RODAK_IDENTITY_WITH_RETIREMENT=1 CONFIG_SOC_CPU_CORES_NUM=2)
target_link_libraries(voice_identity_integration_host PUBLIC Threads::Threads ${MBEDCRYPTO_LIBRARY} rodakos_task_retirement_host)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(voice_identity_integration_host PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(rodakos_voice_identity_integration_tests integration_test.cc "${ROOT}/tests/app_model/test_main.cc")
target_link_libraries(rodakos_voice_identity_integration_tests PRIVATE voice_identity_integration_host)
add_test(NAME rodakos_voice_identity_integration COMMAND rodakos_voice_identity_integration_tests)
set_tests_properties(rodakos_voice_identity_integration PROPERTIES TIMEOUT 40)
add_executable(rodakos_voice_identity_fixture fixture_cli.cc)
target_link_libraries(rodakos_voice_identity_fixture PRIVATE voice_identity_integration_host)
