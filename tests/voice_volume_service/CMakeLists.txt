cmake_minimum_required(VERSION 3.16)
project(rodakos_voice_volume_service_tests LANGUAGES C CXX)

enable_testing()
find_package(Threads REQUIRED)
set(APP_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/../app_model")
set(MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../main")

add_executable(rodakos_voice_volume_service_tests
    voice_volume_service_test.cc
    host_runtime.cc
    fake_audio_focus.cc
    "${APP_MODEL}/test_main.cc"
    "${APP_MODEL}/third_party/cjson/cJSON.c"
    "${MAIN}/phone_os/voice_assistant_service.cc"
    "${MAIN}/phone_os/voice_volume_mcp.cc"
    "${MAIN}/phone_os/voice_assistant_reconnect_coordinator.cc"
    "${MAIN}/phone_os/voice_assistant_transport.cc"
    "${MAIN}/phone_os/voice_conversation_policy.cc"
    "${MAIN}/phone_os/voice_barge_in_policy.cc"
    "${MAIN}/phone_os/realtime_voice_contract.cc"
    "${MAIN}/phone_os/realtime_voice_reconnect_policy.cc"
    "${MAIN}/phone_os/audio_output_service.cc"
    "${MAIN}/rodakos_adapters/audio_codec_output.cc"
)
target_include_directories(rodakos_voice_volume_service_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${APP_MODEL}/fakes"
    "${APP_MODEL}"
    "${APP_MODEL}/third_party/cjson"
    "${MAIN}"
)
target_compile_features(rodakos_voice_volume_service_tests PRIVATE cxx_std_17)
target_link_libraries(rodakos_voice_volume_service_tests PRIVATE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_voice_volume_service_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME rodakos_voice_volume_service COMMAND rodakos_voice_volume_service_tests)
set_tests_properties(rodakos_voice_volume_service PROPERTIES TIMEOUT 30)
