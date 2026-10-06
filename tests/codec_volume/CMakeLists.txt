cmake_minimum_required(VERSION 3.16)

project(rodakos_codec_volume_tests LANGUAGES C CXX)
enable_testing()

get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAKOS_CODEC_COMPONENT_DIR
    "${RODAKOS_ROOT}/managed_components/espressif__esp_codec_dev"
    CACHE PATH "Resolved esp_codec_dev component with original sources and headers")
option(RODAKOS_CODEC_VOLUME_USE_UPSTREAM "Run regressions against the unpatched upstream source" OFF)

if(RODAKOS_CODEC_VOLUME_USE_UPSTREAM)
    set(RODAKOS_CODEC_SOURCE "${RODAKOS_CODEC_COMPONENT_DIR}/esp_codec_dev.c")
    set(RODAKOS_I2S_SOURCE "${RODAKOS_CODEC_COMPONENT_DIR}/platform/audio_codec_data_i2s.c")
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(RODAKOS_CODEC_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/esp_codec_dev.c")
    set(RODAKOS_I2S_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/platform/audio_codec_data_i2s.c")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${RODAKOS_ROOT}/tools/prepare_codec_volume_patch.py"
            --component-dir "${RODAKOS_CODEC_COMPONENT_DIR}"
            --lock-file "${RODAKOS_ROOT}/dependencies.lock"
            --project-manifest "${RODAKOS_ROOT}/main/idf_component.yml"
            --output "${RODAKOS_CODEC_SOURCE}"
        RESULT_VARIABLE RODAKOS_PATCH_RESULT
        OUTPUT_VARIABLE RODAKOS_PATCH_OUTPUT
        ERROR_VARIABLE RODAKOS_PATCH_ERROR
    )
    if(NOT RODAKOS_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Codec patch preparation failed: ${RODAKOS_PATCH_OUTPUT}${RODAKOS_PATCH_ERROR}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${RODAKOS_ROOT}/tools/prepare_codec_volume_patch.py"
        "${RODAKOS_ROOT}/patches/esp_codec_dev/1.5.7/provenance.json"
        "${RODAKOS_ROOT}/patches/esp_codec_dev/1.5.7/set_out_vol.c"
        "${RODAKOS_ROOT}/patches/esp_codec_dev/1.5.7/open.c"
        "${RODAKOS_ROOT}/patches/esp_codec_dev/1.5.7/set_fs.c"
        "${RODAKOS_ROOT}/patches/esp_codec_dev/1.5.7/i2s_format_fault.c"
        "${RODAKOS_ROOT}/dependencies.lock"
        "${RODAKOS_ROOT}/main/idf_component.yml"
        "${RODAKOS_CODEC_COMPONENT_DIR}/esp_codec_dev.c"
        "${RODAKOS_CODEC_COMPONENT_DIR}/platform/audio_codec_data_i2s.c"
        "${RODAKOS_CODEC_COMPONENT_DIR}/idf_component.yml"
        "${RODAKOS_CODEC_COMPONENT_DIR}/.component_hash"
    )
endif()

add_executable(rodakos_codec_volume_tests
    ../app_model/test_main.cc
    codec_volume_test.cc
    codec_i2s_failure_test.cc
    "${RODAKOS_CODEC_SOURCE}"
    "${RODAKOS_I2S_SOURCE}"
    "${RODAKOS_CODEC_COMPONENT_DIR}/audio_codec_sw_vol.c"
    "${RODAKOS_CODEC_COMPONENT_DIR}/esp_codec_dev_if.c"
)
target_include_directories(rodakos_codec_volume_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}/../app_model"
    "${RODAKOS_CODEC_COMPONENT_DIR}/include"
    "${RODAKOS_CODEC_COMPONENT_DIR}/interface"
    "${RODAKOS_CODEC_COMPONENT_DIR}"
)
target_compile_features(rodakos_codec_volume_tests PRIVATE c_std_11 cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_codec_volume_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(UNIX)
    target_link_libraries(rodakos_codec_volume_tests PRIVATE m)
endif()
add_test(NAME rodakos_codec_volume COMMAND rodakos_codec_volume_tests)
