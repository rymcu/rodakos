cmake_minimum_required(VERSION 3.20)
project(rodakos_voice_prepare_priority_transport LANGUAGES C CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto REQUIRED)
find_library(MBEDX509_LIBRARY mbedx509 REQUIRED)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(TRUST "${ROOT}/tests/server_trust")
set(MAIN "${ROOT}/main")
set(RODAK_PREPARE_TRANSPORT_SOURCE "${MAIN}/phone_os/realtime_voice_transport.cc" CACHE FILEPATH
    "Complete transport TU; may select a frozen precise negative control")
set(HOST_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/fakes" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${TRUST}/fakes" "${TRUST}" "${ROOT}/tests/app_model"
    "${ROOT}/tests/app_model/third_party/cjson" "${MAIN}")

add_library(prepare_cloud_host STATIC
    "${TRUST}/host_runtime.cc"
    "${ROOT}/tests/app_model/third_party/cjson/cJSON.c"
    "${MAIN}/phone_os/device_cloud_config.cc"
    "${MAIN}/phone_os/server_trust.cc"
    "${MAIN}/phone_os/server_trust_route.cc"
    "${MAIN}/phone_os/server_trust_transport.cc"
    "${MAIN}/phone_os/server_trust_discovery.cc"
    "${MAIN}/phone_os/device_pairing_protocol.cc"
    "${MAIN}/phone_os/serial_provisioning_protocol.cc"
    "${MAIN}/phone_os/serial_provisioning_binding.cc"
    "${MAIN}/phone_os/realtime_voice_contract.cc"
    "${MAIN}/phone_os/voice_assistant_transport.cc")
target_include_directories(prepare_cloud_host PRIVATE ${HOST_INCLUDES})
target_compile_features(prepare_cloud_host PRIVATE cxx_std_20)
target_compile_definitions(prepare_cloud_host PRIVATE TEST_CA_PATH="${TRUST}/test-ca.pem")

foreach(flavor IN ITEMS test off)
    add_library(prepare_transport_${flavor} OBJECT "${RODAK_PREPARE_TRANSPORT_SOURCE}")
    target_compile_features(prepare_transport_${flavor} PRIVATE cxx_std_20)
    target_include_directories(prepare_transport_${flavor} PRIVATE ${HOST_INCLUDES})
    target_compile_options(prepare_transport_${flavor} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
target_compile_definitions(prepare_transport_test PRIVATE RODAKOS_RELEASE_TESTS=1)
# Keep the OFF specimen free of sanitizer file-path registration strings.
target_compile_options(prepare_transport_off PRIVATE -fno-sanitize=all)

add_executable(voice_prepare_priority_transport_tests transport_test.cc sdk_runtime.cc
    "${ROOT}/tests/app_model/test_main.cc"
    "${MAIN}/phone_os/voice_prepare_priority_observer.cc"
    $<TARGET_OBJECTS:prepare_transport_test>)
target_compile_features(voice_prepare_priority_transport_tests PRIVATE cxx_std_20)
target_include_directories(voice_prepare_priority_transport_tests PRIVATE ${HOST_INCLUDES})
target_compile_definitions(voice_prepare_priority_transport_tests PRIVATE RODAKOS_RELEASE_TESTS=1)
target_compile_options(voice_prepare_priority_transport_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(voice_prepare_priority_transport_tests PRIVATE prepare_cloud_host
    "${MBEDX509_LIBRARY}" "${MBEDCRYPTO_LIBRARY}")
target_link_options(voice_prepare_priority_transport_tests PRIVATE
    -Wl,--wrap=_ZN7rodakos24DeviceCloudConfigService18PrepareVoiceConfigERNS_17DeviceCloudConfigERKSt8functionIFbvEEPNS_19CloudDiagnosticCodeE)
add_test(NAME voice_prepare_priority_transport COMMAND voice_prepare_priority_transport_tests)
set_tests_properties(voice_prepare_priority_transport PROPERTIES TIMEOUT 20)
add_test(NAME voice_prepare_priority_transport_off_absence
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_off.py"
    --object "$<TARGET_OBJECTS:prepare_transport_off>" --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}")
set_tests_properties(voice_prepare_priority_transport_off_absence PROPERTIES TIMEOUT 20)
