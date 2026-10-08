cmake_minimum_required(VERSION 3.20)
project(rodakos_voice_prepare_priority_diagnostic_tests LANGUAGES CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(voice_prepare_priority_diagnostic_driver
    diagnostic_driver.cc
    "${RODAKOS_ROOT}/main/phone_os/voice_prepare_priority_diagnostic.cc")
target_compile_features(voice_prepare_priority_diagnostic_driver PRIVATE cxx_std_20)
target_compile_definitions(voice_prepare_priority_diagnostic_driver PRIVATE RODAKOS_RELEASE_TESTS=1)
target_include_directories(voice_prepare_priority_diagnostic_driver PRIVATE "${RODAKOS_ROOT}/main")
target_compile_options(voice_prepare_priority_diagnostic_driver PRIVATE -Wall -Wextra -Werror)
add_test(NAME voice_prepare_priority_diagnostic
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_output.py"
            "$<TARGET_FILE:voice_prepare_priority_diagnostic_driver>")
set_tests_properties(voice_prepare_priority_diagnostic PROPERTIES TIMEOUT 20)
