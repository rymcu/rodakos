cmake_minimum_required(VERSION 3.20)
project(rodakos_voice_lifecycle_diagnostic_tests LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(source "${ROOT}/main/phone_os/voice_lifecycle_diagnostic.cc")
add_executable(voice_lifecycle_diagnostic_tests lifecycle_test.cc "${source}")
target_compile_features(voice_lifecycle_diagnostic_tests PRIVATE cxx_std_20)
target_compile_definitions(voice_lifecycle_diagnostic_tests PRIVATE RODAKOS_RELEASE_TESTS=1)
target_include_directories(voice_lifecycle_diagnostic_tests PRIVATE "${ROOT}/main" "${ROOT}/tests/app_model")
target_compile_options(voice_lifecycle_diagnostic_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(voice_lifecycle_diagnostic_tests PRIVATE Threads::Threads)
add_library(voice_lifecycle_production OBJECT "${source}")
target_include_directories(voice_lifecycle_production PRIVATE "${ROOT}/main")
target_compile_features(voice_lifecycle_production PRIVATE cxx_std_20)
# This is the ordinary macro-off object; sanitizer module-registration symbols
# would otherwise make even a genuinely empty TU nonempty.
target_compile_options(voice_lifecycle_production PRIVATE -fno-sanitize=all)
add_test(NAME voice_lifecycle_diagnostic COMMAND voice_lifecycle_diagnostic_tests)
set_tests_properties(voice_lifecycle_diagnostic PROPERTIES TIMEOUT 20)
add_test(NAME voice_lifecycle_wire_and_production_absence
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_wire.py"
    --executable "$<TARGET_FILE:voice_lifecycle_diagnostic_tests>"
    --production-object "$<TARGET_OBJECTS:voice_lifecycle_production>")
set_tests_properties(voice_lifecycle_wire_and_production_absence PROPERTIES TIMEOUT 20)
