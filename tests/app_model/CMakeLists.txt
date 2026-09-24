cmake_minimum_required(VERSION 3.16)

project(rodakos_app_model_tests LANGUAGES C CXX)

enable_testing()

add_executable(rodakos_app_model_tests
    test_main.cc
    registry_test.cc
    host_test.cc
    navigation_test.cc
    home_layout_test.cc
    wol_test.cc
    voice_conversation_policy_test.cc
    voice_barge_in_policy_test.cc
    voice_pcm_assembler_test.cc
    voice_aec_diagnostic_capture_test.cc
    voice_wake_settings_test.cc
    voice_identity_test.cc
    serial_provisioning_frame_test.cc
    mqtt_credential_refresh_policy_test.cc
    device_pairing_policy_test.cc
    device_pairing_protocol_test.cc
    realtime_voice_contract_test.cc
    battery_level_model_test.cc
    wifi_connection_policy_test.cc
    ../../main/phone_os/phone_app_registry.cc
    ../../main/phone_os/phone_app_host.cc
    ../../main/phone_os/phone_navigation.cc
    ../../main/apps/home/home_layout_model.cc
    ../../main/apps/home/home_layout_codec.cc
    ../../main/apps/home/home_layout_store.cc
    ../../main/apps/wol/wol_device_codec.cc
    ../../main/phone_os/wake_on_lan_packet.cc
    ../../main/phone_os/voice_conversation_policy.cc
    ../../main/phone_os/voice_barge_in_policy.cc
    ../../main/phone_os/voice_pcm_assembler.cc
    ../../main/phone_os/voice_aec_diagnostic_capture.cc
    ../../main/phone_os/voice_wake_settings.cc
    ../../main/phone_os/voice_identity.cc
    ../../main/phone_os/serial_provisioning_protocol.cc
    ../../main/phone_os/mqtt_credential_refresh_policy.cc
    ../../main/phone_os/device_pairing_protocol.cc
    ../../main/phone_os/realtime_voice_contract.cc
    ../../main/phone_os/realtime_voice_reconnect_policy.cc
    ../../main/phone_os/realtime_voice_transport_harness.cc
    ../../main/phone_os/battery_level_model.cc
    ../../main/rodakos_adapters/wifi_connection_policy.cc
)

target_include_directories(rodakos_app_model_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../main"
)

target_compile_features(rodakos_app_model_tests PRIVATE cxx_std_17)

set(TEST_CJSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson")
target_sources(rodakos_app_model_tests PRIVATE "${TEST_CJSON_DIR}/cJSON.c")
target_include_directories(rodakos_app_model_tests PRIVATE "${TEST_CJSON_DIR}")

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(rodakos_app_model_tests PRIVATE m Threads::Threads)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_app_model_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(NAME rodakos_app_model COMMAND rodakos_app_model_tests)
