cmake_minimum_required(VERSION 3.20)
project(rodakos_screen_jpeg_allocator_tests LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ADAPTER_SOURCE "${RODAKOS_ROOT}/main/phone_os/screen_jpeg_allocation.cc" CACHE FILEPATH
    "Production adapter; override only for explicit negative mutation tests")
add_executable(rodakos_screen_jpeg_allocator_tests
    allocator_test.cc host_heap.cc original_allocators.cc "${ADAPTER_SOURCE}")
target_include_directories(rodakos_screen_jpeg_allocator_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes" "${RODAKOS_ROOT}/main/phone_os")
target_compile_features(rodakos_screen_jpeg_allocator_tests PRIVATE cxx_std_20)
target_compile_options(rodakos_screen_jpeg_allocator_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rodakos_screen_jpeg_allocator_tests PRIVATE Threads::Threads)
foreach(symbol jpeg_calloc jpeg_calloc_inner jpeg_calloc_align jpeg_calloc_align_inner)
    target_link_options(rodakos_screen_jpeg_allocator_tests PRIVATE "-Wl,--wrap=${symbol}")
endforeach()
add_test(NAME rodakos_screen_jpeg_allocator COMMAND rodakos_screen_jpeg_allocator_tests)
add_test(NAME rodakos_screen_jpeg_allocator_audit
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_abi_check.py)
set_tests_properties(rodakos_screen_jpeg_allocator PROPERTIES TIMEOUT 20)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ADAPTER_SOURCE}")
file(SHA256 "${ADAPTER_SOURCE}" adapter_sha)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
    "{\n  \"screen_jpeg_allocation.cc\": \"${adapter_sha}\"\n}\n")
