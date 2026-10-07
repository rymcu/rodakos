cmake_minimum_required(VERSION 3.20)
project(rodakos_display_control_ack_service_tests LANGUAGES C CXX)

enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(service_source "${RODAKOS_ROOT}/main/phone_os/webrtc_display_service.cc")
set(RODAK_DISPLAY_NEGATIVE_SOURCE "" CACHE FILEPATH "Generated source for an isolated negative-control build")
set(test_mode "complete-production-translation-unit")
if(RODAK_DISPLAY_NEGATIVE_SOURCE)
    get_filename_component(service_source "${RODAK_DISPLAY_NEGATIVE_SOURCE}" ABSOLUTE)
    if(NOT EXISTS "${service_source}")
        message(FATAL_ERROR "Negative-control source does not exist")
    endif()
    set(test_mode "complete-translation-unit-negative-control")
endif()
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
    "{\n  \"mode\": \"${test_mode}\",\n  \"webrtc_display_service.cc\": \"${service_sha}\",\n  \"webrtc_display_service.h\": \"${header_sha}\",\n  \"display_control_ack_tracker.h\": \"${tracker_sha}\",\n  \"remote_input_controller.cc\": \"${input_sha}\"\n}\n")

add_executable(rodakos_display_control_ack_service_tests
    display_control_ack_service_test.cc
    host_runtime.cc
    "${service_source}"
    "${input_source}"
    "${RODAKOS_ROOT}/managed_components/espressif__cjson/cJSON/cJSON.c"
    test_main.cc)
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
if(NOT RODAK_DISPLAY_NEGATIVE_SOURCE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME rodakos_display_preopen_negative_controls
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
        --output "${CMAKE_CURRENT_BINARY_DIR}/negative-controls"
        --c-flags=${CMAKE_C_FLAGS} --cxx-flags=${CMAKE_CXX_FLAGS}
        --link-flags=${CMAKE_EXE_LINKER_FLAGS})
    set_tests_properties(rodakos_display_preopen_negative_controls PROPERTIES TIMEOUT 180)
endif()
