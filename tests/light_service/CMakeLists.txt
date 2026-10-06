cmake_minimum_required(VERSION 3.16)
project(rodakos_light_service_tests LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
set(MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../main")
set(APP_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/../app_model")

add_library(light_service_host STATIC
    host_driver.cc
    "${MAIN}/phone_os/light_service.cc"
    "${MAIN}/rodakos_adapters/board_device_adapter.cc")
target_include_directories(light_service_host BEFORE PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${APP_MODEL}/fakes"
    "${APP_MODEL}"
    "${MAIN}")
target_compile_features(light_service_host PUBLIC cxx_std_20)
target_link_libraries(light_service_host PUBLIC Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(light_service_host PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(rodakos_light_service_tests light_service_test.cc "${APP_MODEL}/test_main.cc")
target_link_libraries(rodakos_light_service_tests PRIVATE light_service_host)
add_test(NAME rodakos_light_service COMMAND rodakos_light_service_tests)
set_tests_properties(rodakos_light_service PROPERTIES TIMEOUT 30)
