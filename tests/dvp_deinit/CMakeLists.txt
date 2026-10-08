cmake_minimum_required(VERSION 3.20)
project(rodakos_dvp_deinit_tests LANGUAGES C)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(RODAKOS_IDF_PATH "$ENV{RODAKOS_IDF_PATH}" CACHE PATH "Reviewed ESP-IDF 6.0.2")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_host.py"
    --output "${generated}" --idf-path "${RODAKOS_IDF_PATH}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "DVP host preparation refused: ${output}${error}")
endif()
foreach(variant IN ITEMS patched upstream)
    add_executable(dvp_${variant} test_main.c)
    target_compile_definitions(dvp_${variant} PRIVATE RODAK_DVP_SOURCE="${generated}/${variant}.c")
    target_include_directories(dvp_${variant} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(dvp_${variant} PRIVATE c_std_11)
    target_compile_options(dvp_${variant} PRIVATE -UNDEBUG -Wall -Wextra -Werror -Wno-unused-parameter)
endforeach()
foreach(scenario normal video_failure sccb_failure sensor_failure port_failure lookup_failure
                 unrelated_flags init_cleanup_failure create_failure retry_after_sensor_failure retry_after_port_failure)
    add_test(NAME dvp_${scenario} COMMAND dvp_patched ${scenario})
    set_tests_properties(dvp_${scenario} PROPERTIES TIMEOUT 15)
endforeach()
add_test(NAME dvp_upstream_negative COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_upstream.py" --baseline "$<TARGET_FILE:dvp_upstream>"
    --patched "$<TARGET_FILE:dvp_patched>" --output "${CMAKE_CURRENT_BINARY_DIR}/upstream-comparison")
set_tests_properties(dvp_upstream_negative PROPERTIES TIMEOUT 45)
add_test(NAME dvp_generator COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_generator.py" --idf-path "${RODAKOS_IDF_PATH}")
set_tests_properties(dvp_generator PROPERTIES TIMEOUT 45)
file(GLOB inputs "${CMAKE_CURRENT_SOURCE_DIR}/*.py" "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${root}/patches/dvp_deinit/2.3.0/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${inputs}
    "${root}/tools/prepare_dvp_deinit_patch.py"
    "${root}/managed_components/espressif__esp_video/src/esp_video_init.c"
    "${RODAKOS_IDF_PATH}/components/esp_driver_cam/dvp/src/esp_cam_ctlr_dvp_cam.c")
