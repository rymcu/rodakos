cmake_minimum_required(VERSION 3.20)
project(rodakos_voice_volume_service_tests LANGUAGES C CXX)

enable_testing()
find_package(Threads REQUIRED)
set(APP_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/../app_model")
set(MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../main")
set(RODAK_TASK_RETIREMENT_BUILD_TESTS OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../task_retirement" "${CMAKE_CURRENT_BINARY_DIR}/task-retirement")
set(VOICE_ASSISTANT_SOURCE "${MAIN}/phone_os/voice_assistant_service.cc" CACHE FILEPATH "Complete production or negative-control assistant TU")
set(VOICE_ASSISTANT_BASELINE_INCLUDE "" CACHE PATH "Frozen full old-source header tree")
option(RODAK_ASSISTANT_LEGACY_BASELINE "Compile only pre-snapshot tests for the explicit old-source control" OFF)
set(assistant_header "${MAIN}/phone_os/voice_assistant_service.h")
if(VOICE_ASSISTANT_BASELINE_INCLUDE)
    set(assistant_header "${VOICE_ASSISTANT_BASELINE_INCLUDE}/phone_os/voice_assistant_service.h")
endif()
file(SHA256 "${VOICE_ASSISTANT_SOURCE}" source_sha)
file(SHA256 "${assistant_header}" header_sha)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
    "{\n  \"source\": \"${source_sha}\",\n  \"header\": \"${header_sha}\"\n}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VOICE_ASSISTANT_SOURCE}" "${assistant_header}")

add_executable(rodakos_voice_volume_service_tests
    voice_volume_service_test.cc
    allocation_probe.cc
    host_runtime.cc
    fake_audio_focus.cc
    test_main.cc
    "${APP_MODEL}/third_party/cjson/cJSON.c"
    "${VOICE_ASSISTANT_SOURCE}"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/../task_retirement/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${APP_MODEL}/fakes"
    "${APP_MODEL}"
    "${APP_MODEL}/third_party/cjson"
    "${MAIN}"
)
if(VOICE_ASSISTANT_BASELINE_INCLUDE)
    target_include_directories(rodakos_voice_volume_service_tests BEFORE PRIVATE "${VOICE_ASSISTANT_BASELINE_INCLUDE}")
endif()
target_compile_features(rodakos_voice_volume_service_tests PRIVATE cxx_std_20)
if(RODAK_ASSISTANT_LEGACY_BASELINE)
    target_compile_definitions(rodakos_voice_volume_service_tests PRIVATE RODAK_ASSISTANT_LEGACY_BASELINE=1)
else()
    target_sources(rodakos_voice_volume_service_tests PRIVATE delayed_recording_failure.cc)
    set_source_files_properties(delayed_recording_failure.cc PROPERTIES COMPILE_OPTIONS "-fno-access-control")
endif()
target_compile_definitions(rodakos_voice_volume_service_tests PRIVATE CONFIG_SOC_CPU_CORES_NUM=2)
target_link_libraries(rodakos_voice_volume_service_tests PRIVATE Threads::Threads rodakos_task_retirement_host)
target_link_options(rodakos_voice_volume_service_tests PRIVATE "-Wl,--wrap=_Znwm" "-Wl,--wrap=_Znam")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_voice_volume_service_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME rodakos_voice_volume_service COMMAND rodakos_voice_volume_service_tests)
set_tests_properties(rodakos_voice_volume_service PROPERTIES TIMEOUT 30)
