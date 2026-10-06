# Compile a verified build-local copy; resolved managed sources remain untouched.
idf_component_get_property(ws_component_dir espressif__esp_websocket_client COMPONENT_DIR)
idf_component_get_property(ws_component_lib espressif__esp_websocket_client COMPONENT_LIB)
idf_build_get_property(ws_patch_python PYTHON)
idf_build_get_property(ws_patch_idf_path IDF_PATH)
set(ws_patch_script "${CMAKE_CURRENT_LIST_DIR}/../tools/prepare_websocket_redirect_patch.py")
set(ws_patch_dir "${CMAKE_CURRENT_LIST_DIR}/../patches/esp_websocket_client/1.8.0")
set(ws_original_source "${ws_component_dir}/esp_websocket_client.c")
set(ws_generated_dir "${CMAKE_BINARY_DIR}/rodak_patches/esp_websocket_client")
set(ws_generated_source "${ws_generated_dir}/esp_websocket_client.c")
set(ws_patch_inputs
    "${ws_patch_script}"
    "${ws_patch_dir}/provenance.json"
    "${ws_patch_dir}/reject_redirect.c"
    "${PROJECT_DIR}/dependencies.lock"
    "${PROJECT_DIR}/main/idf_component.yml"
    "${ws_component_dir}/idf_component.yml"
    "${ws_component_dir}/.component_hash"
    "${ws_component_dir}/CMakeLists.txt"
    "${ws_component_dir}/LICENSE"
    "${ws_original_source}"
    "${ws_component_dir}/include/esp_websocket_client.h"
    "${ws_patch_idf_path}/components/tcp_transport/transport_ws.c"
    "${ws_patch_idf_path}/components/tcp_transport/include/esp_transport_ws.h"
    "${ws_patch_idf_path}/components/esp_common/include/esp_idf_version.h"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ws_patch_inputs})
set(ws_patch_command
    "${ws_patch_python}" "${ws_patch_script}"
    --component-dir "${ws_component_dir}"
    --lock-file "${PROJECT_DIR}/dependencies.lock"
    --project-manifest "${PROJECT_DIR}/main/idf_component.yml"
    --idf-path "${ws_patch_idf_path}"
    --output-dir "${ws_generated_dir}"
)
execute_process(COMMAND ${ws_patch_command}
    RESULT_VARIABLE ws_patch_result OUTPUT_VARIABLE ws_patch_output ERROR_VARIABLE ws_patch_error)
if(NOT ws_patch_result EQUAL 0)
    message(FATAL_ERROR "${ws_patch_output}${ws_patch_error}")
endif()
string(STRIP "${ws_patch_output}" ws_patch_output)
message(STATUS "${ws_patch_output}")
add_custom_command(OUTPUT "${ws_generated_source}"
    COMMAND ${ws_patch_command} DEPENDS ${ws_patch_inputs} VERBATIM)
add_custom_target(rodak_websocket_redirect_overlay DEPENDS "${ws_generated_source}")
add_dependencies(${ws_component_lib} rodak_websocket_redirect_overlay)
get_target_property(ws_sources ${ws_component_lib} SOURCES)
set(ws_replaced_sources "")
set(ws_replacement_count 0)
foreach(ws_source IN LISTS ws_sources)
    get_filename_component(ws_source_absolute "${ws_source}" ABSOLUTE BASE_DIR "${ws_component_dir}")
    if(ws_source_absolute STREQUAL ws_original_source)
        list(APPEND ws_replaced_sources "${ws_generated_source}")
        math(EXPR ws_replacement_count "${ws_replacement_count} + 1")
    else()
        list(APPEND ws_replaced_sources "${ws_source}")
    endif()
endforeach()
if(NOT ws_replacement_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one esp_websocket_client.c in the resolved WebSocket target")
endif()
set_property(TARGET ${ws_component_lib} PROPERTY SOURCES "${ws_replaced_sources}")
