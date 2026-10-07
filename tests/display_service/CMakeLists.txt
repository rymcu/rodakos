cmake_minimum_required(VERSION 3.20)
project(rodakos_display_service_tests LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAK_TASK_RETIREMENT_BUILD_TESTS OFF)
add_subdirectory("${RODAKOS_ROOT}/tests/task_retirement" "${CMAKE_CURRENT_BINARY_DIR}/task-retirement")
set(DISPLAY_SERVICE_SOURCE "${RODAKOS_ROOT}/main/phone_os/display_service.cc" CACHE FILEPATH
    "Production service; override only for explicit negative mutation tests")
set(RODAK_SERVICE_BASELINE_INCLUDE "" CACHE PATH "Original full header tree for an explicit old-source control")
set(service_source "${DISPLAY_SERVICE_SOURCE}")
set(allocator_source "${RODAKOS_ROOT}/main/phone_os/screen_jpeg_allocation.cc")
set(service_header "${RODAKOS_ROOT}/main/phone_os/display_service.h")
if(RODAK_SERVICE_BASELINE_INCLUDE)
    set(service_header "${RODAK_SERVICE_BASELINE_INCLUDE}/phone_os/display_service.h")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${service_source}" "${service_header}" "${allocator_source}")
file(SHA256 "${service_source}" service_sha)
file(SHA256 "${service_header}" header_sha)
file(SHA256 "${allocator_source}" allocator_sha)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
    "{\n  \"mode\": \"complete-production-translation-unit\",\n  \"display_service.cc\": \"${service_sha}\",\n  \"display_service.h\": \"${header_sha}\",\n  \"screen_jpeg_allocation.cc\": \"${allocator_sha}\"\n}\n")
add_executable(rodakos_display_service_tests
    display_service_test.cc host_runtime.cc "${service_source}"
    "${RODAKOS_ROOT}/main/phone_os/screen_jpeg_allocation.cc"
    "${RODAKOS_ROOT}/tests/screen_jpeg_allocator/original_allocators.cc"
    test_main.cc)
target_compile_features(rodakos_display_service_tests PRIVATE cxx_std_20)
target_include_directories(rodakos_display_service_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RODAKOS_ROOT}/tests/screen_jpeg_allocator"
    "${RODAKOS_ROOT}/tests/screen_jpeg_allocator/fakes"
    "${RODAKOS_ROOT}/tests/app_model" "${RODAKOS_ROOT}/main")
target_include_directories(rodakos_display_service_tests BEFORE PRIVATE
    "${RODAKOS_ROOT}/tests/task_retirement/fakes")
if(RODAK_SERVICE_BASELINE_INCLUDE)
    target_include_directories(rodakos_display_service_tests BEFORE PRIVATE "${RODAK_SERVICE_BASELINE_INCLUDE}")
endif()
target_link_libraries(rodakos_display_service_tests PRIVATE Threads::Threads rodakos_task_retirement_host)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "This allocation-injection harness requires Linux with GNU-compatible linker wrapping")
endif()
target_link_options(rodakos_display_service_tests PRIVATE "-Wl,--wrap=_Znwm" "-Wl,--wrap=_Znam")
foreach(symbol jpeg_calloc jpeg_calloc_inner jpeg_calloc_align jpeg_calloc_align_inner)
    target_link_options(rodakos_display_service_tests PRIVATE "-Wl,--wrap=${symbol}")
endforeach()
target_compile_options(rodakos_display_service_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME rodakos_display_service COMMAND rodakos_display_service_tests)
set_tests_properties(rodakos_display_service PROPERTIES TIMEOUT 30)
if(NOT RODAK_SERVICE_BASELINE_INCLUDE AND DISPLAY_SERVICE_SOURCE STREQUAL "${RODAKOS_ROOT}/main/phone_os/display_service.cc")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME rodakos_display_retirement_negative_controls
        COMMAND "${Python3_EXECUTABLE}" "${RODAKOS_ROOT}/tests/display_control_ack_service/run_retirement_negative.py"
        --suite display --output "${CMAKE_CURRENT_BINARY_DIR}/retirement-negative-controls"
        --idf-path "${RODAKOS_IDF_PATH}" --c-flags=${CMAKE_C_FLAGS}
        --cxx-flags=${CMAKE_CXX_FLAGS} --link-flags=${CMAKE_EXE_LINKER_FLAGS})
    set_tests_properties(rodakos_display_retirement_negative_controls PROPERTIES TIMEOUT 300)
endif()
