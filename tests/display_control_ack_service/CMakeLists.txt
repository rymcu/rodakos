cmake_minimum_required(VERSION 3.20)
project(rodakos_display_control_ack_service_tests LANGUAGES C CXX)

enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(service_source "${RODAKOS_ROOT}/main/phone_os/webrtc_display_service.cc")
set(service_header "${RODAKOS_ROOT}/main/phone_os/webrtc_display_service.h")
set(tracker_header "${RODAKOS_ROOT}/main/phone_os/display_control_ack_tracker.h")
set(input_source "${RODAKOS_ROOT}/main/phone_os/remote_input_controller.cc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${service_source}" "${service_header}" "${tracker_header}" "${input_source}")
file(SHA256 "${service_source}" service_sha)
file(SHA256 "${service_header}" header_sha)
file(SHA256 "${tracker_header}" tracker_sha)
file(SHA256 "${input_source}" input_sha)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
    "{\n  \"mode\": \"complete-production-translation-unit\",\n  \"webrtc_display_service.cc\": \"${service_sha}\",\n  \"webrtc_display_service.h\": \"${header_sha}\",\n  \"display_control_ack_tracker.h\": \"${tracker_sha}\",\n  \"remote_input_controller.cc\": \"${input_sha}\"\n}\n")

add_executable(rodakos_display_control_ack_service_tests
    display_control_ack_service_test.cc
    host_runtime.cc
    "${service_source}"
    "${input_source}"
    "${RODAKOS_ROOT}/managed_components/espressif__cjson/cJSON/cJSON.c"
    "${RODAKOS_ROOT}/tests/app_model/test_main.cc")
target_compile_features(rodakos_display_control_ack_service_tests PRIVATE cxx_std_20)
target_include_directories(rodakos_display_control_ack_service_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RODAKOS_ROOT}/tests/app_model"
    "${RODAKOS_ROOT}/main"
    "${RODAKOS_ROOT}/components/esp_peer/include"
    "${RODAKOS_ROOT}/managed_components/espressif__cjson/cJSON")
target_link_libraries(rodakos_display_control_ack_service_tests PRIVATE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_display_control_ack_service_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME rodakos_display_control_ack_service
    COMMAND rodakos_display_control_ack_service_tests)
set_tests_properties(rodakos_display_control_ack_service PROPERTIES TIMEOUT 40)
