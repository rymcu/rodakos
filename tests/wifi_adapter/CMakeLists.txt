cmake_minimum_required(VERSION 3.16)
project(rodakos_wifi_adapter_tests LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(rodakos_wifi_adapter_tests
    wifi_adapter_test.cc host_runtime.cc
    "${RODAKOS_ROOT}/tests/app_model/test_main.cc"
    "${RODAKOS_ROOT}/main/rodakos_adapters/wifi_adapter.cc"
    "${RODAKOS_ROOT}/main/rodakos_adapters/wifi_connection_policy.cc")
target_include_directories(rodakos_wifi_adapter_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes" "${RODAKOS_ROOT}/tests/app_model"
    "${RODAKOS_ROOT}/main")
target_compile_features(rodakos_wifi_adapter_tests PRIVATE cxx_std_17)
target_link_libraries(rodakos_wifi_adapter_tests PRIVATE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_wifi_adapter_tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME rodakos_wifi_adapter COMMAND rodakos_wifi_adapter_tests)
set_tests_properties(rodakos_wifi_adapter PROPERTIES TIMEOUT 30)
