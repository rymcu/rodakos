cmake_minimum_required(VERSION 3.20)
project(rodakos_serial_provisioning_tests LANGUAGES CXX)
enable_testing()
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
add_executable(serial_provisioning_tests
    ../app_model/test_main.cc
    ../app_model/serial_provisioning_frame_test.cc
    binding_proof_test.cc
    ../../main/phone_os/serial_provisioning_protocol.cc
    ../../main/phone_os/serial_provisioning_binding.cc)
target_include_directories(serial_provisioning_tests PRIVATE ../app_model ../../main)
target_compile_features(serial_provisioning_tests PRIVATE cxx_std_17)
target_compile_options(serial_provisioning_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(serial_provisioning_tests PRIVATE "${MBEDCRYPTO_LIBRARY}")
add_test(NAME serial_provisioning COMMAND serial_provisioning_tests)
