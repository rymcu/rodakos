cmake_minimum_required(VERSION 3.16)
project(rodakos_websocket_redirect_patch_tests LANGUAGES C)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAKOS_IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "Reviewed ESP-IDF 6.0.2 source tree")
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract_functions.py"
    --idf-path "${RODAKOS_IDF_PATH}" --output-dir "${generated_dir}"
    RESULT_VARIABLE patch_result OUTPUT_VARIABLE patch_output ERROR_VARIABLE patch_error)
if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "WebSocket test overlay refused: ${patch_output}${patch_error}")
endif()
file(GLOB patch_inputs "${RODAKOS_ROOT}/patches/esp_websocket_client/1.8.0/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${patch_inputs}
    "${CMAKE_CURRENT_SOURCE_DIR}/extract_functions.py"
    "${RODAKOS_ROOT}/tools/prepare_websocket_redirect_patch.py"
    "${RODAKOS_ROOT}/dependencies.lock"
    "${RODAKOS_ROOT}/main/idf_component.yml"
    "${RODAKOS_ROOT}/managed_components/espressif__esp_websocket_client/esp_websocket_client.c"
    "${RODAKOS_ROOT}/managed_components/espressif__esp_websocket_client/include/esp_websocket_client.h"
    "${RODAKOS_ROOT}/managed_components/espressif__esp_websocket_client/idf_component.yml"
    "${RODAKOS_ROOT}/managed_components/espressif__esp_websocket_client/.component_hash"
    "${RODAKOS_ROOT}/managed_components/espressif__esp_websocket_client/CMakeLists.txt"
    "${RODAKOS_ROOT}/managed_components/espressif__esp_websocket_client/LICENSE"
    "${RODAKOS_IDF_PATH}/components/tcp_transport/transport_ws.c"
    "${RODAKOS_IDF_PATH}/components/tcp_transport/include/esp_transport_ws.h"
    "${RODAKOS_IDF_PATH}/components/esp_common/include/esp_idf_version.h"
)
foreach(target rodakos_websocket_redirect_tests rodakos_websocket_upstream_tests)
    add_executable(${target} redirect_test.c)
    target_include_directories(${target} PRIVATE "${generated_dir}")
    target_compile_features(${target} PRIVATE c_std_11)
    target_link_libraries(${target} PRIVATE Threads::Threads)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endforeach()
target_compile_definitions(rodakos_websocket_upstream_tests PRIVATE WS_USE_UPSTREAM=1)
foreach(suite redirect status missing_location reconnect lifecycle baseline)
    add_test(NAME websocket_${suite} COMMAND rodakos_websocket_redirect_tests ${suite})
    set_tests_properties(websocket_${suite} PROPERTIES TIMEOUT 20)
endforeach()
foreach(suite redirect status missing_location)
    add_test(NAME websocket_upstream_${suite}_negative COMMAND rodakos_websocket_upstream_tests ${suite})
    set_tests_properties(websocket_upstream_${suite}_negative PROPERTIES WILL_FAIL TRUE TIMEOUT 20)
endforeach()
