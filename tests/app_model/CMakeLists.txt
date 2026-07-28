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
    ../../main/phone_os/phone_app_registry.cc
    ../../main/phone_os/phone_app_host.cc
    ../../main/phone_os/phone_navigation.cc
    ../../main/apps/home/home_layout_model.cc
    ../../main/apps/home/home_layout_codec.cc
    ../../main/apps/home/home_layout_store.cc
    ../../main/apps/wol/wol_device_codec.cc
    ../../main/phone_os/wake_on_lan_packet.cc
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
