cmake_minimum_required(VERSION 3.20)
project(rodakos_camera_teardown_diagnostics_tests LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(DIAGNOSTICS_SOURCE
    "${RODAKOS_ROOT}/main/phone_os/camera-teardown-diagnostics.cc")

add_library(camera_teardown_diagnostics OBJECT "${DIAGNOSTICS_SOURCE}")
add_library(camera_teardown_interleavings OBJECT "${DIAGNOSTICS_SOURCE}")
foreach(target camera_teardown_diagnostics camera_teardown_interleavings)
    target_include_directories(${target} PUBLIC "${RODAKOS_ROOT}/main/phone_os")
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endforeach()
# Only this extra host object redirects compiler intrinsics to deterministic
# scheduling hooks. Production source has no test branches or test-only API.
target_compile_options(camera_teardown_interleavings PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/atomic_interleaving.h")

add_executable(rodakos_camera_teardown_diagnostics_tests diagnostics_test.cc c_api_test.c)
target_compile_features(rodakos_camera_teardown_diagnostics_tests PRIVATE cxx_std_17 c_std_11)
target_compile_options(rodakos_camera_teardown_diagnostics_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(rodakos_camera_teardown_diagnostics_tests PRIVATE
    camera_teardown_diagnostics Threads::Threads)
foreach(test empty c_api sequence capacity pending invalid_count invalid_commit concurrent)
    add_test(NAME camera_teardown_${test}
        COMMAND rodakos_camera_teardown_diagnostics_tests "${test}")
    set_tests_properties(camera_teardown_${test} PROPERTIES TIMEOUT 20)
endforeach()

add_executable(rodakos_camera_teardown_interleavings_tests interleavings_test.cc)
target_compile_features(rodakos_camera_teardown_interleavings_tests PRIVATE cxx_std_17)
target_compile_options(rodakos_camera_teardown_interleavings_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(rodakos_camera_teardown_interleavings_tests PRIVATE
    camera_teardown_interleavings Threads::Threads)
foreach(test collision suspended changed)
    add_test(NAME camera_teardown_${test}
        COMMAND rodakos_camera_teardown_interleavings_tests "${test}")
    set_tests_properties(camera_teardown_${test} PROPERTIES TIMEOUT 20)
endforeach()

add_executable(rodakos_camera_teardown_abi abi_probe.c)
target_compile_features(rodakos_camera_teardown_abi PRIVATE c_std_11)
target_compile_options(rodakos_camera_teardown_abi PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(rodakos_camera_teardown_abi PRIVATE camera_teardown_diagnostics)
add_test(NAME camera_teardown_abi COMMAND rodakos_camera_teardown_abi)
add_test(NAME camera_teardown_link_checker
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_linked_check.py)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DIAGNOSTICS_SOURCE}")
file(SHA256 "${DIAGNOSTICS_SOURCE}" diagnostics_sha)
file(SHA256 "${RODAKOS_ROOT}/main/phone_os/camera-teardown-diagnostics.h" header_sha)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
    "{\n  \"camera-teardown-diagnostics.cc\": \"${diagnostics_sha}\",\n  \"camera-teardown-diagnostics.h\": \"${header_sha}\"\n}\n")
