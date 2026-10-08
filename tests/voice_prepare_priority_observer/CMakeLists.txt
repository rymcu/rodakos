cmake_minimum_required(VERSION 3.20)
project(rodakos_voice_prepare_priority_observer_tests LANGUAGES CXX)
enable_testing()
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(voice_prepare_priority_observer_tests
    observer_test.cc
    "${RODAKOS_ROOT}/main/phone_os/voice_prepare_priority_observer.cc")
target_compile_features(voice_prepare_priority_observer_tests PRIVATE cxx_std_20)
target_compile_definitions(voice_prepare_priority_observer_tests PRIVATE
    RODAKOS_RELEASE_TESTS=1 RODAKOS_PREPARE_PRIORITY_OBSERVER_HOST_TEST=1)
target_include_directories(voice_prepare_priority_observer_tests PRIVATE
    host_sdk "${RODAKOS_ROOT}/main")
target_compile_options(voice_prepare_priority_observer_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME voice_prepare_priority_observer COMMAND voice_prepare_priority_observer_tests)
set_tests_properties(voice_prepare_priority_observer PROPERTIES TIMEOUT 20)
