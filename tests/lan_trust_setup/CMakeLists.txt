cmake_minimum_required(VERSION 3.20)
project(rodakos_lan_trust_setup_tests LANGUAGES C CXX)
enable_testing()
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
set(RODAKOS_IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "Reviewed ESP-IDF 6.0.2 source tree")

# 直接编译 ESP-IDF 自带的 esp_srp，而不是重写一份：测试向量必须来自设备端实际运行的代码。
# 源码哈希锁定 v6.0.2（7101770d），IDF 升级时须重新审阅 esp_srp 的填充与长度处理并更新此表。
set(srp "${RODAKOS_IDF_PATH}/components/protocomm")
set(srp_sources
    "src/crypto/srp6a/esp_srp.c=7ab3085f214250c5dacd6003fddbd3842b3d1c884f2d67905d693987a2f67545"
    "src/crypto/srp6a/esp_srp_mpi.c=b87436884952ce5ee9e9d13cbdef5f16e8663bbd95c7d78ce1b0af40681b3c12"
    "src/crypto/srp6a/esp_srp_mpi.h=a99ab0e470b858726cf9f9170f39fd1b73926dbf7112f9c9f483c6001016aa11"
    "include/crypto/srp6a/esp_srp.h=c4e58e8dc697f5a3904ba3fdf250feec9b9d631faff5dce4a96925336ec35ad8")
foreach(entry IN LISTS srp_sources)
    string(REPLACE "=" ";" parts "${entry}")
    list(GET parts 0 relative)
    list(GET parts 1 expected)
    if(NOT EXISTS "${srp}/${relative}")
        message(FATAL_ERROR "esp_srp source missing: ${srp}/${relative}")
    endif()
    file(SHA256 "${srp}/${relative}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "esp_srp source drift: ${relative} ${actual}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${srp}/${relative}")
endforeach()

add_executable(lan_trust_setup_tests
    ../app_model/test_main.cc
    lan_trust_setup_test.cc
    protocol_vectors_test.cc
    trust_setup_protocol_test.cc
    ../../main/phone_os/trust_setup_protocol.cc
    ../../main/phone_os/server_trust.cc
    ../../main/phone_os/server_trust_route.cc
    host_random.c
    srp_harness.cc
    ../app_model/third_party/cjson/cJSON.c
    "${srp}/src/crypto/srp6a/esp_srp.c"
    "${srp}/src/crypto/srp6a/esp_srp_mpi.c")
target_include_directories(lan_trust_setup_tests BEFORE PRIVATE
    fakes . ../../main ../app_model ../app_model/third_party/cjson "${srp}/include/crypto/srp6a" "${srp}/src/crypto/srp6a")
target_compile_definitions(lan_trust_setup_tests PRIVATE
    LAN_TRUST_SETUP_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/srp6a-esp-vectors.json"
    LAN_TRUST_SETUP_PROTOCOL_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/trust-setup-protocol-vectors.json"
    LAN_TRUST_SETUP_TEST_CA="${CMAKE_CURRENT_SOURCE_DIR}/../server_trust/test-ca.pem")
target_compile_features(lan_trust_setup_tests PRIVATE cxx_std_20)
set_source_files_properties(
    "${srp}/src/crypto/srp6a/esp_srp.c" "${srp}/src/crypto/srp6a/esp_srp_mpi.c"
    PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(lan_trust_setup_test.cc protocol_vectors_test.cc
    trust_setup_protocol_test.cc ../../main/phone_os/trust_setup_protocol.cc srp_harness.cc host_random.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic")
find_library(MBEDX509_LIBRARY mbedx509 REQUIRED)
target_link_libraries(lan_trust_setup_tests PRIVATE "${MBEDX509_LIBRARY}" "${MBEDCRYPTO_LIBRARY}")
add_test(NAME lan_trust_setup COMMAND lan_trust_setup_tests)
set_tests_properties(lan_trust_setup PROPERTIES TIMEOUT 120)
