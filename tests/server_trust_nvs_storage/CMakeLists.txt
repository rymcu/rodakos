cmake_minimum_required(VERSION 3.20)
project(rodakos_server_trust_nvs_storage_tests LANGUAGES C CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
find_library(MBEDX509_LIBRARY mbedx509 REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAKOS_IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "Reviewed ESP-IDF 6.0.2 source tree")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_sdk.py"
    --idf-path "${RODAKOS_IDF_PATH}"
    RESULT_VARIABLE sdk_result OUTPUT_VARIABLE sdk_output ERROR_VARIABLE sdk_error)
if(NOT sdk_result EQUAL 0)
    message(FATAL_ERROR "NVS storage source verification failed: ${sdk_output}${sdk_error}")
endif()
message(STATUS "${sdk_output}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk-provenance.json" sdk_provenance)
string(JSON sdk_file_count LENGTH "${sdk_provenance}" source_hashes_lf)
math(EXPR sdk_file_last "${sdk_file_count} - 1")
set(sdk_inputs "")
foreach(index RANGE ${sdk_file_last})
    string(JSON relative MEMBER "${sdk_provenance}" source_hashes_lf ${index})
    list(APPEND sdk_inputs "${RODAKOS_IDF_PATH}/${relative}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${sdk_inputs}
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk-provenance.json" "${CMAKE_CURRENT_SOURCE_DIR}/verify_sdk.py")
set(nvs "${RODAKOS_IDF_PATH}/components/nvs_flash")
add_executable(server_trust_nvs_storage_tests
    ../app_model/test_main.cc storage_test.cc
    ../app_model/third_party/cjson/cJSON.c
    "${RODAKOS_ROOT}/main/phone_os/server_trust.cc"
    "${RODAKOS_ROOT}/main/phone_os/server_trust_route.cc"
    "${nvs}/src/nvs_storage.cpp" "${nvs}/src/nvs_page.cpp"
    "${nvs}/src/nvs_pagemanager.cpp" "${nvs}/src/nvs_types.cpp"
    "${nvs}/src/nvs_item_hash_list.cpp"
    "${RODAKOS_IDF_PATH}/components/esp_rom/linux/esp_rom_crc.c")
target_include_directories(server_trust_nvs_storage_tests BEFORE PRIVATE
    fakes ../app_model ../app_model/third_party/cjson "${RODAKOS_ROOT}/main"
    "${nvs}/src" "${nvs}/include" "${nvs}/private_include"
    "${RODAKOS_IDF_PATH}/components/esp_common/include"
    "${RODAKOS_IDF_PATH}/components/esp_rom/include")
target_compile_definitions(server_trust_nvs_storage_tests PRIVATE NO_DEBUG_STORAGE=1
    CONFIG_NVS_ASSERT_ERROR_CHECK=0
    PUBLIC_TEST_CA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../server_trust/test-ca.pem")
target_compile_options(server_trust_nvs_storage_tests PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/fakes/compat.h" -Wall -Wextra)
target_compile_features(server_trust_nvs_storage_tests PRIVATE cxx_std_17)
target_link_libraries(server_trust_nvs_storage_tests PRIVATE "${MBEDX509_LIBRARY}" "${MBEDCRYPTO_LIBRARY}")
add_test(NAME server_trust_nvs_storage COMMAND server_trust_nvs_storage_tests)
set_tests_properties(server_trust_nvs_storage PROPERTIES TIMEOUT 40)
