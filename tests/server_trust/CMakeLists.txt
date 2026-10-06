cmake_minimum_required(VERSION 3.20)
project(rodakos_server_trust_tests LANGUAGES C CXX)
enable_testing()
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
find_library(MBEDX509_LIBRARY mbedx509 REQUIRED)
add_executable(server_trust_tests
    ../app_model/test_main.cc
    server_trust_test.cc
    host_runtime.cc
    ../app_model/third_party/cjson/cJSON.c
    ../../main/phone_os/server_trust.cc
    ../../main/phone_os/server_trust_route.cc
    ../../main/phone_os/server_trust_transport.cc
    ../../main/phone_os/server_trust_discovery.cc
    ../../main/phone_os/device_cloud_config.cc
    ../../main/phone_os/device_pairing_protocol.cc
    ../../main/phone_os/realtime_voice_contract.cc
    ../../main/phone_os/serial_provisioning_protocol.cc
    ../../main/phone_os/serial_provisioning_binding.cc)
target_include_directories(server_trust_tests BEFORE PRIVATE
    fakes . ../app_model ../app_model/third_party/cjson ../../main)
target_compile_definitions(server_trust_tests PRIVATE TEST_CA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test-ca.pem")
target_compile_features(server_trust_tests PRIVATE cxx_std_20)
target_compile_options(server_trust_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(server_trust_tests PRIVATE "${MBEDX509_LIBRARY}" "${MBEDCRYPTO_LIBRARY}")
add_test(NAME server_trust COMMAND server_trust_tests)
