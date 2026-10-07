cmake_minimum_required(VERSION 3.20)
project(rodakos_voice_audio_frontend_identity_tests LANGUAGES C CXX)

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAK_VOICE_FRONTEND_SOURCE "${RODAKOS_ROOT}/main/phone_os/voice_audio_frontend.cc"
    CACHE FILEPATH "Complete production frontend TU or frozen negative control")
set(RODAK_VOICE_FRONTEND_INCLUDE_ROOT "" CACHE PATH "Frozen matching frontend header override")
option(RODAK_FRONTEND_LEGACY_PROBE "Compile only the legacy-compatible retirement probe" OFF)
option(RODAK_FRONTEND_NEGATIVE_CHILD "Skip recursive negative controls" OFF)
add_subdirectory("${RODAKOS_ROOT}/tests/task_retirement" "task_retirement")

add_executable(rodakos_voice_audio_frontend_identity_tests
    test_main.cc
    voice_audio_frontend_identity_test.cc
    retirement_test.cc
    allocation_observer.cc
    host_runtime.cc
    "${RODAK_VOICE_FRONTEND_SOURCE}"
    "${RODAKOS_ROOT}/main/phone_os/voice_identity.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_pcm_assembler.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_aec_diagnostic_capture.cc")
target_compile_features(rodakos_voice_audio_frontend_identity_tests PRIVATE cxx_std_20)
target_compile_definitions(rodakos_voice_audio_frontend_identity_tests PRIVATE
    CONFIG_SOC_CPU_CORES_NUM=2
    CONFIG_USE_DEVICE_AEC=0)
if(RODAK_FRONTEND_LEGACY_PROBE)
    target_compile_definitions(rodakos_voice_audio_frontend_identity_tests PRIVATE
        RODAK_FRONTEND_LEGACY_PROBE=1)
endif()
target_include_directories(rodakos_voice_audio_frontend_identity_tests BEFORE PRIVATE
    "${RODAKOS_ROOT}/tests/task_retirement/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RODAKOS_ROOT}/tests/app_model"
    "${RODAKOS_ROOT}/main")
if(RODAK_VOICE_FRONTEND_INCLUDE_ROOT)
    target_include_directories(rodakos_voice_audio_frontend_identity_tests BEFORE PRIVATE
        "${RODAK_VOICE_FRONTEND_INCLUDE_ROOT}")
endif()
target_link_libraries(rodakos_voice_audio_frontend_identity_tests PRIVATE rodakos_task_retirement_host)
target_link_options(rodakos_voice_audio_frontend_identity_tests PRIVATE
    -Wl,--wrap=_ZdlPv -Wl,--wrap=_ZdlPvm)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_voice_audio_frontend_identity_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME rodakos_voice_audio_frontend_identity
    COMMAND rodakos_voice_audio_frontend_identity_tests)
set_tests_properties(rodakos_voice_audio_frontend_identity PROPERTIES TIMEOUT 40)
if(NOT RODAK_FRONTEND_NEGATIVE_CHILD)
    add_test(NAME voice_frontend_retirement_negative_controls
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
        --idf-path "${RODAKOS_IDF_PATH}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/negative-controls")
    set_tests_properties(voice_frontend_retirement_negative_controls PROPERTIES TIMEOUT 180)
endif()
