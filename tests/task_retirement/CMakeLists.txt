cmake_minimum_required(VERSION 3.20)
project(rodakos_task_retirement_tests LANGUAGES C CXX)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAKOS_IDF_PATH "$ENV{RODAKOS_IDF_PATH}" CACHE PATH "Reviewed ESP-IDF 6.0.2")
if(NOT RODAKOS_IDF_PATH)
    set(RODAKOS_IDF_PATH "$ENV{IDF_PATH}")
endif()
set(retirement_tests_default OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(retirement_tests_default ON)
endif()
option(RODAK_TASK_RETIREMENT_BUILD_TESTS "Build standalone retirement tests" ${retirement_tests_default})
option(RODAK_RETIREMENT_NEGATIVE_CHILD "Skip recursive negative-control test registration" OFF)
set(RODAK_RETIREMENT_SOURCE "${RODAKOS_ROOT}/main/phone_os/task-retirement.cc" CACHE FILEPATH "Complete production retirement TU or negative-control copy")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_idf.py"
    --idf-path "${RODAKOS_IDF_PATH}" --output-dir "${generated}"
    --retirement-source "${RODAK_RETIREMENT_SOURCE}"
    RESULT_VARIABLE prepared OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT prepared EQUAL 0)
    message(FATAL_ERROR "WithCaps fixture refused: ${output}${error}")
endif()
add_library(rodakos_task_retirement_host STATIC
    host_runtime.cc "${RODAK_RETIREMENT_SOURCE}" "${generated}/idf_withcaps.c")
target_compile_features(rodakos_task_retirement_host PUBLIC cxx_std_20 PRIVATE c_std_11)
target_include_directories(rodakos_task_retirement_host
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private_fakes" "${RODAKOS_ROOT}/main/phone_os"
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/fakes" "${CMAKE_CURRENT_SOURCE_DIR}" "${RODAKOS_ROOT}/main")
target_compile_options(rodakos_task_retirement_host PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(rodakos_task_retirement_host PUBLIC Threads::Threads)
set_source_files_properties("${RODAK_RETIREMENT_SOURCE}" "${generated}/idf_withcaps.c" PROPERTIES
    COMPILE_DEFINITIONS "heap_caps_malloc=retirement_host_heap_malloc;heap_caps_calloc=retirement_host_heap_calloc;heap_caps_free=retirement_host_heap_free")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/prepare_idf.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/host_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/task_retirement_host.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/retirement_test.cc"
    "${RODAK_RETIREMENT_SOURCE}" "${RODAKOS_ROOT}/main/phone_os/task-retirement.h"
    "${RODAKOS_ROOT}/sdkconfig"
    "${RODAKOS_IDF_PATH}/components/freertos/esp_additions/idf_additions.c"
    "${RODAKOS_IDF_PATH}/components/freertos/esp_additions/include/freertos/idf_additions.h"
    "${RODAKOS_IDF_PATH}/components/freertos/esp_additions/freertos_tasks_c_additions.h"
    "${RODAKOS_IDF_PATH}/components/freertos/heap_idf.c"
    "${RODAKOS_IDF_PATH}/components/freertos/config/include/freertos/FreeRTOSConfig.h"
    "${RODAKOS_IDF_PATH}/components/freertos/config/xtensa/include/freertos/FreeRTOSConfig_arch.h"
    "${RODAKOS_IDF_PATH}/components/freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos/portmacro.h")

if(RODAK_TASK_RETIREMENT_BUILD_TESTS)
    enable_testing()
    add_executable(rodakos_task_retirement_tests retirement_test.cc)
    target_link_libraries(rodakos_task_retirement_tests PRIVATE rodakos_task_retirement_host)
    target_compile_options(rodakos_task_retirement_tests PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    foreach(scenario publication destructor concurrent autonomous drain cancel capacity reference core self_join pool_failure ordinary_tasks)
        add_test(NAME task_retirement_${scenario} COMMAND rodakos_task_retirement_tests ${scenario})
        set_tests_properties(task_retirement_${scenario} PROPERTIES TIMEOUT 15)
    endforeach()
    add_test(NAME task_retirement_real_idf_self_delete_red
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/assert_abort.py"
        --executable "$<TARGET_FILE:rodakos_task_retirement_tests>" --scenario legacy_self_delete
        --expected "EXPECTED_IDF_CLEANUP_CREATE_REJECTED name=prvTaskDeleteWithCapsTask"
        --expected "Failed to create the task to delete the current task"
        --forbidden "UNEXPECTED_LEGACY_LOCAL_DESTRUCTOR"
        --output "${CMAKE_CURRENT_BINARY_DIR}/legacy-self-delete.json")
    set_tests_properties(task_retirement_real_idf_self_delete_red PROPERTIES TIMEOUT 15)
    if(NOT RODAK_RETIREMENT_NEGATIVE_CHILD)
        add_test(NAME task_retirement_negative_controls
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
            --idf-path "${RODAKOS_IDF_PATH}" --output "${CMAKE_CURRENT_BINARY_DIR}/negative-controls")
        set_tests_properties(task_retirement_negative_controls PROPERTIES TIMEOUT 180)
    endif()
endif()
