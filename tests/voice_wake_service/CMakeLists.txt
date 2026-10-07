cmake_minimum_required(VERSION 3.20)
project(rodakos_voice_wake_service_tests LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAK_TASK_RETIREMENT_BUILD_TESTS OFF)
add_subdirectory("${RODAKOS_ROOT}/tests/task_retirement" "${CMAKE_CURRENT_BINARY_DIR}/task-retirement")
set(VOICE_WAKE_SOURCE "${RODAKOS_ROOT}/main/phone_os/voice_wake_service.cc" CACHE FILEPATH "Complete wake production TU")
set(VOICE_WAKE_BASELINE_INCLUDE "" CACHE PATH "Frozen full old-source header tree")
set(wake_header "${RODAKOS_ROOT}/main/phone_os/voice_wake_service.h")
if(VOICE_WAKE_BASELINE_INCLUDE)
    set(wake_header "${VOICE_WAKE_BASELINE_INCLUDE}/phone_os/voice_wake_service.h")
endif()
file(SHA256 "${VOICE_WAKE_SOURCE}" source_sha)
file(SHA256 "${wake_header}" header_sha)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
    "{\n  \"source\": \"${source_sha}\",\n  \"header\": \"${header_sha}\"\n}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VOICE_WAKE_SOURCE}" "${wake_header}")
add_executable(rodakos_voice_wake_service_tests
    voice_wake_service_test.cc host_runtime.cc
    test_main.cc
    "${VOICE_WAKE_SOURCE}"
    "${RODAKOS_ROOT}/main/phone_os/voice_wake_settings.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_identity.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_identity_record.cc"
    "${RODAKOS_ROOT}/main/phone_os/time_service.cc"
    "${RODAKOS_ROOT}/managed_components/espressif__cjson/cJSON/cJSON.c")
target_include_directories(rodakos_voice_wake_service_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/retirement_fakes"
    "${RODAKOS_ROOT}/tests/task_retirement/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes" "${RODAKOS_ROOT}/tests/app_model"
    "${RODAKOS_ROOT}/main" "${RODAKOS_ROOT}/managed_components/espressif__cjson/cJSON")
if(VOICE_WAKE_BASELINE_INCLUDE)
    target_include_directories(rodakos_voice_wake_service_tests BEFORE PRIVATE "${VOICE_WAKE_BASELINE_INCLUDE}")
endif()
target_compile_features(rodakos_voice_wake_service_tests PRIVATE cxx_std_20)
target_compile_definitions(rodakos_voice_wake_service_tests PRIVATE CONFIG_SOC_CPU_CORES_NUM=2)
target_link_libraries(rodakos_voice_wake_service_tests PRIVATE Threads::Threads rodakos_task_retirement_host)
target_link_options(rodakos_voice_wake_service_tests PRIVATE "-Wl,--wrap=gettimeofday")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_voice_wake_service_tests PRIVATE -Wall -Wextra -Werror -Wno-unused-variable)
endif()
add_test(NAME rodakos_voice_wake_service COMMAND rodakos_voice_wake_service_tests)
set_tests_properties(rodakos_voice_wake_service PROPERTIES TIMEOUT 120)
option(RODAK_WAKE_NEGATIVE_CHILD "Skip recursive old-source negative registration" OFF)
if(NOT RODAK_WAKE_NEGATIVE_CHILD)
    add_test(NAME rodakos_voice_wake_legacy_negative
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
        --idf-path "${RODAKOS_IDF_PATH}" --output "${CMAKE_CURRENT_BINARY_DIR}/legacy-negative")
    set_tests_properties(rodakos_voice_wake_legacy_negative PROPERTIES TIMEOUT 180)
endif()
