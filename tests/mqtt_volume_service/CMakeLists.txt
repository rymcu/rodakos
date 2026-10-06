cmake_minimum_required(VERSION 3.16)
project(rodakos_mqtt_volume_service_tests LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
set(APP_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/../app_model")
set(MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../main")

add_library(mqtt_volume_host STATIC
    host_runtime.cc
    "${APP_MODEL}/third_party/cjson/cJSON.c"
    "${MAIN}/phone_os/unified_mqtt_service.cc"
    "${MAIN}/phone_os/mqtt_volume_effect.cc"
    "${MAIN}/phone_os/mqtt_credential_refresh_policy.cc"
    "${MAIN}/phone_os/audio_output_service.cc"
    "${MAIN}/phone_os/realtime_voice_contract.cc"
    "${MAIN}/phone_os/voice_identity.cc"
    "${MAIN}/rodakos_adapters/audio_codec_output.cc"
)
target_include_directories(mqtt_volume_host BEFORE PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${APP_MODEL}/fakes"
    "${APP_MODEL}"
    "${APP_MODEL}/third_party/cjson"
    "${MAIN}"
)
target_compile_features(mqtt_volume_host PUBLIC cxx_std_17)
target_link_libraries(mqtt_volume_host PUBLIC Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mqtt_volume_host PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(rodakos_mqtt_volume_service_tests mqtt_volume_service_test.cc "${APP_MODEL}/test_main.cc")
target_link_libraries(rodakos_mqtt_volume_service_tests PRIVATE mqtt_volume_host)
add_test(NAME rodakos_mqtt_volume_service COMMAND rodakos_mqtt_volume_service_tests)
set_tests_properties(rodakos_mqtt_volume_service PROPERTIES TIMEOUT 40)

add_executable(rodakos_mqtt_volume_fixture mqtt_volume_fixture.cc)
target_link_libraries(rodakos_mqtt_volume_fixture PRIVATE mqtt_volume_host)
