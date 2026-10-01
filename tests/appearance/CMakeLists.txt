cmake_minimum_required(VERSION 3.20)
project(rodakos_appearance_tests LANGUAGES C CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures.py"
    "${CMAKE_CURRENT_BINARY_DIR}" RESULT_VARIABLE generated)
if(NOT generated EQUAL 0)
    message(FATAL_ERROR "Failed to generate appearance signing fixtures")
endif()
add_executable(appearance_tests ../app_model/test_main.cc package_test.cc crypto_test.cc policy_test.cc json_allocator_test.cc
    ../../components/rodak_appearance/rodak_appearance.cc
    ../../components/rodak_appearance/rodak_appearance_crypto.cc
    ../../components/rodak_appearance/rodak_appearance_policy.cc
    ../../components/rodak_appearance/rodak_appearance_json.c
    ../app_model/third_party/cjson/cJSON.c)
target_include_directories(appearance_tests PRIVATE ../app_model ../app_model/third_party/cjson
    ../../components/rodak_appearance/include ../../components/rodak_ota_state/include "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(appearance_tests PRIVATE cxx_std_17)
target_compile_options(appearance_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(appearance_tests PRIVATE "${MBEDCRYPTO_LIBRARY}")
add_test(NAME appearance_package_and_signature COMMAND appearance_tests)

add_executable(appearance_contract_check contract_check.cc
    ../../components/rodak_appearance/rodak_appearance.cc
    ../../components/rodak_appearance/rodak_appearance_json.c)
target_include_directories(appearance_contract_check PRIVATE ../app_model/third_party/cjson
    ../../components/rodak_appearance/include)
target_compile_features(appearance_contract_check PRIVATE cxx_std_17)
target_compile_options(appearance_contract_check PRIVATE -Wall -Wextra -Wpedantic)
