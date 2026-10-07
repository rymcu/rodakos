cmake_minimum_required(VERSION 3.20)
project(rodakos_aes_dma_cleanup_tests LANGUAGES C)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAKOS_IDF_PATH "$ENV{RODAKOS_IDF_PATH}" CACHE PATH "Reviewed ESP-IDF 6.0.2")
set(RODAK_AES_VARIANT "patched" CACHE STRING "baseline or patched")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_host.py"
    --idf-path "${RODAKOS_IDF_PATH}" --output-dir "${CMAKE_CURRENT_BINARY_DIR}/generated"
    --variant "${RODAK_AES_VARIANT}"
    RESULT_VARIABLE prepared OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT prepared EQUAL 0)
    message(FATAL_ERROR "AES host preparation refused: ${output}${error}")
endif()
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_library(aes_production OBJECT "${generated}/esp_aes_dma_core.c" "${generated}/esp_aes.c" "${generated}/esp_aes_common.c")
target_compile_options(aes_production PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/host_fakes.h")
target_compile_definitions(aes_production PRIVATE free=host_tracked_free)
target_include_directories(aes_production PRIVATE "${generated}/fakes" "${generated}/real" "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(rodakos_aes_dma_cleanup_tests aes_dma_cleanup_test.c host_runtime.c)
target_include_directories(rodakos_aes_dma_cleanup_tests PRIVATE "${generated}/fakes" "${generated}/real" .)
target_link_libraries(rodakos_aes_dma_cleanup_tests PRIVATE aes_production)
foreach(target aes_production rodakos_aes_dma_cleanup_tests)
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_options(${target} PRIVATE -UNDEBUG -Wall -Wextra -Werror -Wno-unused-parameter)
endforeach()
if(RODAK_AES_VARIANT STREQUAL "patched")
    add_library(aes_upstream OBJECT
        "${RODAKOS_IDF_PATH}/components/mbedtls/port/aes/dma/esp_aes_dma_core.c"
        "${RODAKOS_IDF_PATH}/components/mbedtls/port/aes/esp_aes.c"
        "${RODAKOS_IDF_PATH}/components/mbedtls/port/aes/esp_aes_common.c")
    target_compile_options(aes_upstream PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/host_fakes.h")
    target_compile_definitions(aes_upstream PRIVATE free=host_tracked_free)
    add_executable(rodakos_aes_dma_upstream_tests aes_dma_cleanup_test.c host_runtime.c)
    target_link_libraries(rodakos_aes_dma_upstream_tests PRIVATE aes_upstream)
    foreach(target aes_upstream rodakos_aes_dma_upstream_tests)
        target_include_directories(${target} PRIVATE "${generated}/fakes" "${generated}/real" "${CMAKE_CURRENT_SOURCE_DIR}")
        target_compile_features(${target} PRIVATE c_std_11)
        target_compile_options(${target} PRIVATE -UNDEBUG -Wall -Wextra -Werror -Wno-unused-parameter)
    endforeach()
    add_test(NAME aes_dma_upstream_negative_and_normal_parity
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_upstream.py"
        --baseline "$<TARGET_FILE:rodakos_aes_dma_upstream_tests>"
        --patched "$<TARGET_FILE:rodakos_aes_dma_cleanup_tests>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/upstream-comparison")
    set_tests_properties(aes_dma_upstream_negative_and_normal_parity PROPERTIES TIMEOUT 30)
    add_test(NAME aes_dma_generator COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_generator.py"
        --idf-path "${RODAKOS_IDF_PATH}")
    set_tests_properties(aes_dma_generator PROPERTIES TIMEOUT 120)
endif()
file(GLOB aes_test_inputs "${CMAKE_CURRENT_SOURCE_DIR}/*.py" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB aes_patch_inputs "${RODAKOS_ROOT}/patches/aes_dma_cleanup/6.0.2/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${aes_test_inputs} ${aes_patch_inputs} "${RODAKOS_ROOT}/tools/prepare_aes_dma_cleanup_patch.py"
    "${RODAKOS_IDF_PATH}/components/mbedtls/port/aes/dma/esp_aes_dma_core.c"
    "${RODAKOS_IDF_PATH}/components/mbedtls/port/aes/esp_aes.c"
    "${RODAKOS_IDF_PATH}/components/mbedtls/port/aes/esp_aes_common.c")
foreach(scenario output_failure input_failure output_only_failure success)
    add_test(NAME aes_dma_${scenario} COMMAND rodakos_aes_dma_cleanup_tests ${scenario})
    set_tests_properties(aes_dma_${scenario} PROPERTIES TIMEOUT 15)
endforeach()
