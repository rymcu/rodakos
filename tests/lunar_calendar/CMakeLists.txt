cmake_minimum_required(VERSION 3.16)
project(rodakos_lunar_calendar_tests LANGUAGES CXX)
enable_testing()
set(MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../main")
set(APP_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/../app_model")
add_executable(rodakos_lunar_calendar_tests
    lunar_calendar_test.cc
    "${APP_MODEL}/test_main.cc"
    "${MAIN}/phone_os/lunar_calendar.cc")
target_include_directories(rodakos_lunar_calendar_tests PRIVATE "${APP_MODEL}" "${MAIN}")
target_compile_features(rodakos_lunar_calendar_tests PRIVATE cxx_std_20)
target_compile_definitions(rodakos_lunar_calendar_tests PRIVATE
    LUNAR_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/reference.txt")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_lunar_calendar_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME rodakos_lunar_calendar COMMAND rodakos_lunar_calendar_tests)
set_tests_properties(rodakos_lunar_calendar PROPERTIES TIMEOUT 30)
