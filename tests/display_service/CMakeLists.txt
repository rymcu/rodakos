cmake_minimum_required(VERSION 3.20)
project(rodakos_display_service_tests LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(service_source "${RODAKOS_ROOT}/main/phone_os/display_service.cc")
set(service_header "${RODAKOS_ROOT}/main/phone_os/display_service.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${service_source}" "${service_header}")
file(SHA256 "${service_source}" service_sha)
file(SHA256 "${service_header}" header_sha)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
    "{\n  \"mode\": \"complete-production-translation-unit\",\n  \"display_service.cc\": \"${service_sha}\",\n  \"display_service.h\": \"${header_sha}\"\n}\n")
add_executable(rodakos_display_service_tests
    display_service_test.cc host_runtime.cc "${service_source}"
    "${RODAKOS_ROOT}/tests/app_model/test_main.cc")
target_compile_features(rodakos_display_service_tests PRIVATE cxx_std_20)
target_include_directories(rodakos_display_service_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RODAKOS_ROOT}/tests/app_model" "${RODAKOS_ROOT}/main")
target_link_libraries(rodakos_display_service_tests PRIVATE Threads::Threads)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "This allocation-injection harness requires Linux with GNU-compatible linker wrapping")
endif()
target_link_options(rodakos_display_service_tests PRIVATE "-Wl,--wrap=_Znwm" "-Wl,--wrap=_Znam")
target_compile_options(rodakos_display_service_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME rodakos_display_service COMMAND rodakos_display_service_tests)
set_tests_properties(rodakos_display_service PROPERTIES TIMEOUT 30)
