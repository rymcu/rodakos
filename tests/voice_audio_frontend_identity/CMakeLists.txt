cmake_minimum_required(VERSION 3.20)
project(rodakos_voice_audio_frontend_identity_tests LANGUAGES C CXX)

enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(rodakos_voice_audio_frontend_identity_tests
    "${RODAKOS_ROOT}/tests/app_model/test_main.cc"
    voice_audio_frontend_identity_test.cc
    host_runtime.cc
    "${RODAKOS_ROOT}/main/phone_os/voice_audio_frontend.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_identity.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_pcm_assembler.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_aec_diagnostic_capture.cc")
target_compile_features(rodakos_voice_audio_frontend_identity_tests PRIVATE cxx_std_20)
target_compile_definitions(rodakos_voice_audio_frontend_identity_tests PRIVATE
    CONFIG_SOC_CPU_CORES_NUM=2
    CONFIG_USE_DEVICE_AEC=0)
target_include_directories(rodakos_voice_audio_frontend_identity_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RODAKOS_ROOT}/tests/app_model"
    "${RODAKOS_ROOT}/main")
target_link_libraries(rodakos_voice_audio_frontend_identity_tests PRIVATE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_voice_audio_frontend_identity_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME rodakos_voice_audio_frontend_identity
    COMMAND rodakos_voice_audio_frontend_identity_tests)
set_tests_properties(rodakos_voice_audio_frontend_identity PROPERTIES TIMEOUT 40)
