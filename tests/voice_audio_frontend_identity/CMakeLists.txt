cmake_minimum_required(VERSION 3.20)
project(rodakos_voice_audio_frontend_identity_tests LANGUAGES C CXX)

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ESP_SR_ROOT "${RODAKOS_ROOT}/managed_components/espressif__esp-sr")
# 所有权结论只覆盖固定 SDK 与已反汇编的模型库；升级时必须重新审计。
set(MN_CONTRACT_FILES
    "src/esp_mn_speech_commands.c|57c14f35dc3107db84d038f9e16d560831106698280895d7bae73a7a3cfd7885"
    "src/include/esp_mn_speech_commands.h|26c86a0195cc24f3e0e8bc26ac9fc346a06cb01828d8d22c8066c60dd4f7b342"
    "include/esp32s3/esp_mn_iface.h|74f031979ac525cfee66e088cc9c856052727d562489b64e3bee6be3b5e05349"
    "lib/esp32s3/libmultinet.a|92431596a614dcc2671a8dd36231f4f4fef5f793e232601ee178134759aa910b")
foreach(CONTRACT IN LISTS MN_CONTRACT_FILES)
    string(REPLACE "|" ";" PARTS "${CONTRACT}")
    list(GET PARTS 0 RELATIVE_PATH)
    list(GET PARTS 1 EXPECTED_HASH)
    file(SHA256 "${ESP_SR_ROOT}/${RELATIVE_PATH}" ACTUAL_HASH)
    if(NOT ACTUAL_HASH STREQUAL EXPECTED_HASH)
        message(FATAL_ERROR "ESP-SR 2.2.2 MultiNet ownership contract changed: ${RELATIVE_PATH}")
    endif()
endforeach()
# 输出字节账本依赖 1MIC 的固定 feed/fetch/ring/WebRTC 二进制合同；升级须重新反汇编。
set(AFE_CONTRACT_FILES
    "lib/esp32s3/libesp_audio_front_end.a|0f8b45d5fe9267479dbe9a35148b4f0820ae61375d134a886215f535d6fac2dd"
    "lib/esp32s3/libesp_audio_processor.a|9499e6548944fc39b40b0246719e7a7446d2e839f72f50a9a35e718785334178"
    "lib/esp32s3/libdl_lib.a|0299cea1f4fde129641069c9d180ca92b5657c9c780693dfe961f2b3d7111d8f")
foreach(CONTRACT IN LISTS AFE_CONTRACT_FILES)
    string(REPLACE "|" ";" PARTS "${CONTRACT}")
    list(GET PARTS 0 RELATIVE_PATH)
    list(GET PARTS 1 EXPECTED_HASH)
    file(SHA256 "${ESP_SR_ROOT}/${RELATIVE_PATH}" ACTUAL_HASH)
    if(NOT ACTUAL_HASH STREQUAL EXPECTED_HASH)
        message(FATAL_ERROR "ESP-SR 2.2.2 AFE output contract changed: ${RELATIVE_PATH}")
    endif()
endforeach()
set(RODAK_VOICE_FRONTEND_SOURCE "${RODAKOS_ROOT}/main/phone_os/voice_audio_frontend.cc"
    CACHE FILEPATH "Complete production frontend TU or frozen negative control")
set(RODAK_VOICE_FRONTEND_INCLUDE_ROOT "" CACHE PATH "Frozen matching frontend header override")
option(RODAK_FRONTEND_LEGACY_PROBE "Compile only the legacy-compatible retirement probe" OFF)
option(RODAK_FRONTEND_NEGATIVE_CHILD "Skip recursive negative controls" OFF)
option(RODAK_FRONTEND_DEVICE_AEC "Exercise the compiled device AEC input contract" ON)
option(RODAK_FRONTEND_OBSERVATION_TESTS "Compile current diagnostic-stage integration probes" ON)
add_subdirectory("${RODAKOS_ROOT}/tests/task_retirement" "task_retirement")

add_executable(rodakos_voice_audio_frontend_identity_tests
    test_main.cc
    voice_audio_frontend_identity_test.cc
    retirement_test.cc
    allocation_observer.cc
    host_runtime.cc
    observation_control.cc
    afe_fetch_control.cc
    afe_fetch_lifecycle_test.cc
    afe_output_flow_test.cc
    mn_runtime.cc
    mn_registry.c
    mn_ownership_test.cc
    "${RODAK_VOICE_FRONTEND_SOURCE}"
    "${RODAKOS_ROOT}/main/phone_os/voice_identity.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_pcm_assembler.cc"
    "${RODAKOS_ROOT}/main/phone_os/voice_aec_diagnostic_capture.cc")
if(RODAK_FRONTEND_OBSERVATION_TESTS)
    target_sources(rodakos_voice_audio_frontend_identity_tests PRIVATE afe_observation_test.cc)
    # Internal gate/snapshot probes only; production TU and header tokens remain unchanged.
    set_source_files_properties(afe_observation_test.cc PROPERTIES COMPILE_OPTIONS "-fno-access-control")
endif()
target_compile_features(rodakos_voice_audio_frontend_identity_tests PRIVATE cxx_std_20)
target_compile_definitions(rodakos_voice_audio_frontend_identity_tests PRIVATE
    RODAK_ESP_SR_COMMANDS_SOURCE="${ESP_SR_ROOT}/src/esp_mn_speech_commands.c"
    CONFIG_SOC_CPU_CORES_NUM=2
    CONFIG_USE_DEVICE_AEC=$<BOOL:${RODAK_FRONTEND_DEVICE_AEC}>)
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
target_include_directories(rodakos_voice_audio_frontend_identity_tests PRIVATE
    "${ESP_SR_ROOT}/include/esp32s3"
    "${ESP_SR_ROOT}/src/include")
if(RODAK_VOICE_FRONTEND_INCLUDE_ROOT)
    target_include_directories(rodakos_voice_audio_frontend_identity_tests BEFORE PRIVATE
        "${RODAK_VOICE_FRONTEND_INCLUDE_ROOT}")
endif()
target_link_libraries(rodakos_voice_audio_frontend_identity_tests PRIVATE rodakos_task_retirement_host)
target_link_options(rodakos_voice_audio_frontend_identity_tests PRIVATE
    -Wl,--wrap=retirement_host_enter_critical
    -Wl,--wrap=_ZdlPv -Wl,--wrap=_ZdlPvm
    -Wl,--wrap=esp_mn_commands_alloc -Wl,--wrap=esp_mn_commands_free
    -Wl,--wrap=esp_mn_commands_clear)
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
    add_test(NAME voice_frontend_multinet_negative_controls
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_mn_negative.py"
        --idf-path "${RODAKOS_IDF_PATH}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/mn-negative-controls")
    set_tests_properties(voice_frontend_multinet_negative_controls PROPERTIES TIMEOUT 180)
    add_test(NAME voice_frontend_afe_negative_controls
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_afe_negative.py"
        --idf-path "${RODAKOS_IDF_PATH}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/afe-negative-controls")
    set_tests_properties(voice_frontend_afe_negative_controls PROPERTIES TIMEOUT 240)
    add_test(NAME voice_frontend_observation_negative_controls
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_observation_negative.py"
        --idf-path "${RODAKOS_IDF_PATH}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/observation-negative-controls")
    set_tests_properties(voice_frontend_observation_negative_controls PROPERTIES TIMEOUT 180)
endif()
