cmake_minimum_required(VERSION 3.16)
project(rodakos_voice_wake_service_tests LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(rodakos_voice_wake_service_tests
    voice_wake_service_test.cc host_runtime.cc
    "${RODAKOS_ROOT}/tests/app_model/test_main.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_wake_service.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_wake_settings.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_identity.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_identity_record.cc"
    "${RODAKOS_ROOT}/main/phone_os/time_service.cc"
    "${RODAKOS_ROOT}/managed_components/espressif__cjson/cJSON/cJSON.c")
target_include_directories(rodakos_voice_wake_service_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes" "${RODAKOS_ROOT}/tests/app_model"
    "${RODAKOS_ROOT}/main" "${RODAKOS_ROOT}/managed_components/espressif__cjson/cJSON")
target_compile_features(rodakos_voice_wake_service_tests PRIVATE cxx_std_17)
target_link_libraries(rodakos_voice_wake_service_tests PRIVATE Threads::Threads)
target_link_options(rodakos_voice_wake_service_tests PRIVATE "-Wl,--wrap=gettimeofday")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_voice_wake_service_tests PRIVATE -Wall -Wextra -Werror -Wno-unused-variable)
endif()
add_test(NAME rodakos_voice_wake_service COMMAND rodakos_voice_wake_service_tests)
set_tests_properties(rodakos_voice_wake_service PROPERTIES TIMEOUT 40)
