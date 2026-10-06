cmake_minimum_required(VERSION 3.16)
project(rodakos_mqtt_volume_service_tests LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
set(APP_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/../app_model")
set(MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../main")

add_library(mqtt_volume_host STATIC
    host_runtime.cc
    ../light_service/host_driver.cc
    "${APP_MODEL}/third_party/cjson/cJSON.c"
    "${MAIN}/phone_os/unified_mqtt_service.cc"
    "${MAIN}/phone_os/mqtt_command_ledger.cc"
    "${MAIN}/phone_os/mqtt_volume_effect.cc"
    "${MAIN}/phone_os/mqtt_light_effect.cc"
    "${MAIN}/phone_os/light_service.cc"
    "${MAIN}/rodakos_adapters/board_device_adapter.cc"
    "${MAIN}/phone_os/mqtt_credential_refresh_policy.cc"
    "${MAIN}/phone_os/audio_output_service.cc"
    "${MAIN}/phone_os/realtime_voice_contract.cc"
    "${MAIN}/phone_os/voice_identity.cc"
    "${MAIN}/rodakos_adapters/audio_codec_output.cc"
)
target_include_directories(mqtt_volume_host BEFORE PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
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
