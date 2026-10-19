cmake_minimum_required(VERSION 3.16)
project(rodakos_mqtt_event_patch_tests LANGUAGES C CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAKOS_IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "Reviewed ESP-IDF 6.0.2 source tree")
if(NOT EXISTS "${RODAKOS_IDF_PATH}/components/esp_event/esp_event.c")
    message(FATAL_ERROR "Pass -DRODAKOS_IDF_PATH=<ESP-IDF 6.0.2 source tree>")
endif()
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract_functions.py"
    --idf-path "${RODAKOS_IDF_PATH}" --output-dir "${generated_dir}"
    RESULT_VARIABLE patch_result OUTPUT_VARIABLE patch_output ERROR_VARIABLE patch_error)
if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "MQTT event test overlay refused: ${patch_output}${patch_error}")
endif()
file(GLOB patch_inputs "${RODAKOS_ROOT}/patches/esp_mqtt/1.0.0/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${patch_inputs}
    "${CMAKE_CURRENT_SOURCE_DIR}/extract_functions.py"
    "${RODAKOS_ROOT}/tools/prepare_mqtt_event_patch.py"
    "${RODAKOS_ROOT}/dependencies.lock"
    "${RODAKOS_ROOT}/main/idf_component.yml"
    "${RODAKOS_ROOT}/managed_components/espressif__mqtt/mqtt_client.c"
    "${RODAKOS_ROOT}/managed_components/espressif__mqtt/lib/include/mqtt_client_priv.h"
    "${RODAKOS_ROOT}/managed_components/espressif__mqtt/include/mqtt_client.h"
    "${RODAKOS_ROOT}/managed_components/espressif__mqtt/lib/include/mqtt_config.h"
    "${RODAKOS_ROOT}/managed_components/espressif__mqtt/idf_component.yml"
    "${RODAKOS_ROOT}/managed_components/espressif__mqtt/.component_hash"
    "${RODAKOS_IDF_PATH}/components/esp_event/esp_event.c"
)
add_executable(rodakos_mqtt_event_patch_tests event_patch_test.cc)
target_include_directories(rodakos_mqtt_event_patch_tests PRIVATE "${generated_dir}")
target_compile_features(rodakos_mqtt_event_patch_tests PRIVATE cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_mqtt_event_patch_tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME rodakos_mqtt_event_patch COMMAND rodakos_mqtt_event_patch_tests)
set_tests_properties(rodakos_mqtt_event_patch PROPERTIES TIMEOUT 40)

# 用未修改的 IDF TLSF 堆验证替换客户端时任务栈落在哪里。
set(idf_heap "${RODAKOS_IDF_PATH}/components/heap")
add_library(rodakos_idf_tlsf_heap STATIC "${idf_heap}/multi_heap.c" "${idf_heap}/tlsf/tlsf.c")
target_include_directories(rodakos_idf_tlsf_heap PUBLIC "${idf_heap}/include"
    PRIVATE "${idf_heap}" "${idf_heap}/tlsf" "${idf_heap}/tlsf/include")
add_executable(rodakos_mqtt_task_storage_layout_tests task_storage_layout_test.cc)
target_link_libraries(rodakos_mqtt_task_storage_layout_tests PRIVATE rodakos_idf_tlsf_heap)
target_compile_features(rodakos_mqtt_task_storage_layout_tests PRIVATE cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_mqtt_task_storage_layout_tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME rodakos_mqtt_task_storage_layout COMMAND rodakos_mqtt_task_storage_layout_tests)
set_tests_properties(rodakos_mqtt_task_storage_layout PROPERTIES TIMEOUT 40)
