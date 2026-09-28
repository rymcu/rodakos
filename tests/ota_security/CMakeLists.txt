cmake_minimum_required(VERSION 3.20)
project(rodakos_ota_security_tests LANGUAGES CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
set(OTA "${CMAKE_CURRENT_SOURCE_DIR}/../../components/rodak_ota_state")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures.py"
    "${CMAKE_CURRENT_BINARY_DIR}" RESULT_VARIABLE generated)
if(NOT generated EQUAL 0)
    message(FATAL_ERROR "Failed to generate isolated OTA cryptographic fixtures")
endif()
add_executable(ota_security_tests
    ../app_model/test_main.cc security_test.cc journal_test.cc
    "${OTA}/rodak_ota_signature.cc" "${OTA}/rodak_ota_signature_format.cc"
    "${OTA}/rodak_ota_state.cc")
target_include_directories(ota_security_tests PRIVATE
    fakes ../app_model "${OTA}/include" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(ota_security_tests PRIVATE cxx_std_17)
target_compile_options(ota_security_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ota_security_tests PRIVATE "${MBEDCRYPTO_LIBRARY}")
add_test(NAME ota_security COMMAND ota_security_tests)
add_executable(ota_fault_tests ../app_model/test_main.cc fault_test.cc "${OTA}/rodak_release_fault.cc")
target_include_directories(ota_fault_tests PRIVATE fakes ../app_model "${OTA}/include")
target_compile_features(ota_fault_tests PRIVATE cxx_std_17)
target_compile_definitions(ota_fault_tests PRIVATE
    RODAK_OTA_FAULT_INJECTION_PHASE="after_image_write" RODAK_RELEASE_TEST_ID="test-1")
add_test(NAME ota_fault_once COMMAND ota_fault_tests)
add_executable(recovery_tests ../app_model/test_main.cc recovery_test.cc
    "${CMAKE_CURRENT_SOURCE_DIR}/../../recovery/main/recovery_main.cc"
    "${OTA}/rodak_ota_state.cc" "${OTA}/rodak_ota_signature.cc" "${OTA}/rodak_ota_signature_format.cc")
target_include_directories(recovery_tests PRIVATE fakes ../app_model "${OTA}/include" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(recovery_tests PRIVATE cxx_std_17)
target_compile_options(recovery_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(recovery_tests PRIVATE -Wl,--wrap=fopen)
target_link_libraries(recovery_tests PRIVATE "${MBEDCRYPTO_LIBRARY}")
add_test(NAME recovery_state_machine COMMAND recovery_tests)
