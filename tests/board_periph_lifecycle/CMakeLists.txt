cmake_minimum_required(VERSION 3.20)
project(rodakos_board_periph_lifecycle_tests LANGUAGES C)

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(SHA256 "${RODAKOS_ROOT}/components/esp_board_manager/src/esp_board_periph.c" source_sha)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
    "{\n  \"esp_board_periph.c\": \"${source_sha}\"\n}\n")

foreach(variant patched legacy)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_host.py"
            --source "${RODAKOS_ROOT}/components/esp_board_manager/src/esp_board_periph.c"
            --output "${generated}/${variant}/esp_board_periph.c"
            --variant "${variant}"
        RESULT_VARIABLE prepared
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT prepared EQUAL 0)
        message(FATAL_ERROR "Board peripheral host preparation refused (${variant}): ${output}${error}")
    endif()
endforeach()

set(RODAKOS_BOARD_INCLUDES
    "${RODAKOS_ROOT}/components/esp_board_manager/include"
    "${RODAKOS_ROOT}/components/esp_board_manager/private_inc")

function(configure_board_periph_target target source)
    add_executable(${target} test_main.c "${source}")
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        ${RODAKOS_BOARD_INCLUDES})
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
    target_compile_options(${target} PRIVATE -UNDEBUG -Wall -Wextra -Werror)
endfunction()

configure_board_periph_target(
    rodakos_board_periph_lifecycle_tests
    "${generated}/patched/esp_board_periph.c")
configure_board_periph_target(
    rodakos_board_periph_lifecycle_legacy_tests
    "${generated}/legacy/esp_board_periph.c")

add_test(NAME board_periph_deinit_failure_retry
    COMMAND rodakos_board_periph_lifecycle_tests deinit_failure_retry)
add_test(NAME board_periph_missing_callback_retry
    COMMAND rodakos_board_periph_lifecycle_tests missing_callback_retry)
add_test(NAME board_periph_legacy_negative
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
        --executable "$<TARGET_FILE:rodakos_board_periph_lifecycle_legacy_tests>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/legacy-negative.json")
set_tests_properties(board_periph_deinit_failure_retry board_periph_missing_callback_retry
    PROPERTIES TIMEOUT 10)
set_tests_properties(board_periph_legacy_negative PROPERTIES TIMEOUT 15)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/prepare_host.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes/esp_err.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes/esp_log.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes/esp_check.h"
    "${RODAKOS_ROOT}/components/esp_board_manager/src/esp_board_periph.c")
