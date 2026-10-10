cmake_minimum_required(VERSION 3.16)
project(rodakos_camera_teardown_patch_tests LANGUAGES C CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAKOS_IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "Reviewed ESP-IDF 6.0.2 source tree")
if(NOT EXISTS "${RODAKOS_IDF_PATH}/components/esp_driver_cam/esp_cam_ctlr.c")
    message(FATAL_ERROR "Pass -DRODAKOS_IDF_PATH=<ESP-IDF 6.0.2 source tree>")
endif()
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(camera_patch_command
    "${Python3_EXECUTABLE}" "${RODAKOS_ROOT}/tools/prepare_camera_teardown_patch.py"
    --video-dir "${RODAKOS_ROOT}/managed_components/espressif__esp_video"
    --sensor-dir "${RODAKOS_ROOT}/managed_components/espressif__esp_cam_sensor"
    --lock-file "${RODAKOS_ROOT}/dependencies.lock"
    --project-manifest "${RODAKOS_ROOT}/main/idf_component.yml"
    --idf-path "${RODAKOS_IDF_PATH}"
    --diagnostics-header "${RODAKOS_ROOT}/main/phone_os/camera-teardown-diagnostics.h"
    --output-dir "${generated_dir}")
execute_process(COMMAND ${camera_patch_command} --print-inputs
    RESULT_VARIABLE inputs_result OUTPUT_VARIABLE inputs_output ERROR_VARIABLE inputs_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT inputs_result EQUAL 0)
    message(FATAL_ERROR "${inputs_output}${inputs_error}")
endif()
string(REPLACE "\r\n" "\n" inputs_output "${inputs_output}")
string(REPLACE "\n" ";" camera_inputs "${inputs_output}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${camera_inputs}
    "${CMAKE_CURRENT_SOURCE_DIR}/extract_functions.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract_functions.py"
    --idf-path "${RODAKOS_IDF_PATH}" --output-dir "${generated_dir}"
    RESULT_VARIABLE extract_result OUTPUT_VARIABLE extract_output ERROR_VARIABLE extract_error)
if(NOT extract_result EQUAL 0)
    message(FATAL_ERROR "Camera test extraction refused: ${extract_output}${extract_error}")
endif()
function(configure_camera_teardown_target target)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${RODAKOS_ROOT}/main/phone_os")
    target_compile_features(${target} PRIVATE c_std_11 cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
    endif()
endfunction()
add_executable(rodakos_camera_teardown_patch_tests teardown_patch_test.cc
    "${generated_dir}/production_functions.c")
configure_camera_teardown_target(rodakos_camera_teardown_patch_tests)
add_executable(rodakos_camera_dma_fault_tests teardown_patch_test.cc
    "${generated_dir}/production_functions.c")
configure_camera_teardown_target(rodakos_camera_dma_fault_tests)
target_compile_definitions(rodakos_camera_dma_fault_tests PRIVATE RODAKOS_CAMERA_DMA_FORCE_4096=1)
add_executable(rodakos_camera_dma_fail_once_tests teardown_patch_test.cc
    "${generated_dir}/production_functions.c")
configure_camera_teardown_target(rodakos_camera_dma_fail_once_tests)
target_compile_definitions(rodakos_camera_dma_fail_once_tests
    PRIVATE RODAKOS_CAMERA_DMA_FAIL_FIRST_RING=1)
foreach(scenario IN ITEMS complete record_drop common_fail_sensor common_fail_interface
        common_fail_stop common_fail_disable common_fail_delete del_fail_gpio_disable
        del_fail_capture del_fail_gpio_remove del_fail_disconnect del_fail_dma_delete
        del_retry_gpio_disable del_retry_capture del_retry_gpio_remove del_retry_disconnect
        del_retry_dma_delete common_retry_disconnect
        del_self_reject
        startup_cleanup startup_cleanup_error null_dma blocked_sensor blocked_stop
        blocked_disable blocked_delete blocked_task blocked_gpio_disable blocked_capture
        blocked_gpio_remove blocked_disconnect blocked_dma_delete
        dma_preferred dma_jpeg_configured dma_ring_fallback dma_desc_fallback dma_exhausted
        dma_exhausted_retry)
    add_test(NAME "camera_teardown_${scenario}" COMMAND rodakos_camera_teardown_patch_tests "${scenario}")
    set_tests_properties("camera_teardown_${scenario}" PROPERTIES TIMEOUT 10)
endforeach()
add_test(NAME camera_teardown_dma_forced_fallback
    COMMAND rodakos_camera_dma_fault_tests dma_forced_fallback)
set_tests_properties(camera_teardown_dma_forced_fallback PROPERTIES TIMEOUT 10)
add_test(NAME camera_teardown_dma_fail_once_retry
    COMMAND rodakos_camera_dma_fail_once_tests dma_fail_once_retry)
set_tests_properties(camera_teardown_dma_fail_once_retry PROPERTIES TIMEOUT 10)
add_test(NAME camera_teardown_generator COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_generator.py" --idf-path "${RODAKOS_IDF_PATH}")
set_tests_properties(camera_teardown_generator PROPERTIES TIMEOUT 60)
