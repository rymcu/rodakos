cmake_minimum_required(VERSION 3.20)
project(rodakos_dvp_rcc_tests LANGUAGES C)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAKOS_IDF_PATH "$ENV{RODAKOS_IDF_PATH}" CACHE PATH "Reviewed ESP-IDF 6.0.2")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_host.py"
    --idf-path "${RODAKOS_IDF_PATH}" --output "${generated}"
    RESULT_VARIABLE prepared OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT prepared EQUAL 0)
    message(FATAL_ERROR "DVP RCC host preparation refused: ${output}${error}")
endif()
foreach(variant IN ITEMS upstream patched)
    add_executable(dvp_rcc_${variant} test_main.c)
    target_compile_definitions(dvp_rcc_${variant} PRIVATE RODAK_DVP_RCC_SOURCE="${generated}/${variant}.c")
    target_include_directories(dvp_rcc_${variant} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(dvp_rcc_${variant} PRIVATE c_std_11)
    target_compile_options(dvp_rcc_${variant} PRIVATE -UNDEBUG -Wall -Wextra -Werror -Wno-unused-parameter)
endforeach()
foreach(scenario single repeat shared_owner invalid)
    add_test(NAME dvp_rcc_${scenario} COMMAND dvp_rcc_patched ${scenario})
    set_tests_properties(dvp_rcc_${scenario} PROPERTIES TIMEOUT 15)
endforeach()
add_test(NAME dvp_rcc_upstream_negative COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_upstream.py"
    --baseline "$<TARGET_FILE:dvp_rcc_upstream>" --patched "$<TARGET_FILE:dvp_rcc_patched>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/upstream-comparison")
set_tests_properties(dvp_rcc_upstream_negative PROPERTIES TIMEOUT 45)
add_test(NAME dvp_rcc_generator COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_generator.py" --idf-path "${RODAKOS_IDF_PATH}")
set_tests_properties(dvp_rcc_generator PROPERTIES TIMEOUT 45)
file(GLOB test_inputs "${CMAKE_CURRENT_SOURCE_DIR}/*.py" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB patch_inputs "${RODAKOS_ROOT}/patches/dvp_rcc/2.3.0/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${test_inputs} ${patch_inputs}
    "${RODAKOS_ROOT}/tools/prepare_dvp_rcc_patch.py"
    "${RODAKOS_IDF_PATH}/components/esp_driver_cam/dvp/src/esp_cam_ctlr_dvp_cam.c"
    "${RODAKOS_IDF_PATH}/components/esp_hw_support/include/esp_private/periph_ctrl.h"
    "${RODAKOS_IDF_PATH}/components/esp_hw_support/periph_ctrl.c")
