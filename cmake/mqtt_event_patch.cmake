# Resolved managed sources remain untouched; compile checked build-local copies.
idf_component_get_property(mqtt_component_dir espressif__mqtt COMPONENT_DIR)
idf_component_get_property(mqtt_component_lib espressif__mqtt COMPONENT_LIB)
idf_build_get_property(mqtt_patch_python PYTHON)
idf_build_get_property(mqtt_patch_idf_path IDF_PATH)
set(mqtt_patch_script "${CMAKE_CURRENT_LIST_DIR}/../tools/prepare_mqtt_event_patch.py")
set(mqtt_patch_dir "${CMAKE_CURRENT_LIST_DIR}/../patches/esp_mqtt/1.0.0")
set(mqtt_original_source "${mqtt_component_dir}/mqtt_client.c")
set(mqtt_generated_dir "${CMAKE_BINARY_DIR}/rodak_patches/esp_mqtt")
set(mqtt_generated_source "${mqtt_generated_dir}/mqtt_client.c")
set(mqtt_generated_header "${mqtt_generated_dir}/mqtt_client_priv.h")
set(mqtt_patch_inputs
    "${mqtt_patch_script}"
    "${mqtt_patch_dir}/provenance.json"
    "${mqtt_patch_dir}/custom_dispatch.c"
    "${mqtt_patch_dir}/run_event_loop.c"
    "${PROJECT_DIR}/dependencies.lock"
    "${PROJECT_DIR}/main/idf_component.yml"
    "${mqtt_component_dir}/idf_component.yml"
    "${mqtt_component_dir}/.component_hash"
    "${mqtt_original_source}"
    "${mqtt_component_dir}/lib/include/mqtt_client_priv.h"
    "${mqtt_component_dir}/lib/include/mqtt_config.h"
    "${mqtt_component_dir}/include/mqtt_client.h"
    "${mqtt_patch_idf_path}/components/esp_event/esp_event.c"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${mqtt_patch_inputs})
set(mqtt_patch_command
    "${mqtt_patch_python}" "${mqtt_patch_script}"
    --component-dir "${mqtt_component_dir}"
    --lock-file "${PROJECT_DIR}/dependencies.lock"
    --project-manifest "${PROJECT_DIR}/main/idf_component.yml"
    --idf-path "${mqtt_patch_idf_path}"
    --output-dir "${mqtt_generated_dir}"
)
execute_process(COMMAND ${mqtt_patch_command}
    RESULT_VARIABLE mqtt_patch_result OUTPUT_VARIABLE mqtt_patch_output ERROR_VARIABLE mqtt_patch_error)
if(NOT mqtt_patch_result EQUAL 0)
    message(FATAL_ERROR "${mqtt_patch_output}${mqtt_patch_error}")
endif()
string(STRIP "${mqtt_patch_output}" mqtt_patch_output)
message(STATUS "${mqtt_patch_output}")
add_custom_command(OUTPUT "${mqtt_generated_source}" "${mqtt_generated_header}"
    COMMAND ${mqtt_patch_command} DEPENDS ${mqtt_patch_inputs} VERBATIM)
add_custom_target(rodak_mqtt_event_overlay DEPENDS "${mqtt_generated_source}" "${mqtt_generated_header}")
add_dependencies(${mqtt_component_lib} rodak_mqtt_event_overlay)
get_target_property(mqtt_sources ${mqtt_component_lib} SOURCES)
set(mqtt_replaced_sources "")
set(mqtt_replacement_count 0)
foreach(mqtt_source IN LISTS mqtt_sources)
    get_filename_component(mqtt_source_absolute "${mqtt_source}" ABSOLUTE BASE_DIR "${mqtt_component_dir}")
    if(mqtt_source_absolute STREQUAL mqtt_original_source)
        list(APPEND mqtt_replaced_sources "${mqtt_generated_source}")
        math(EXPR mqtt_replacement_count "${mqtt_replacement_count} + 1")
    else()
        list(APPEND mqtt_replaced_sources "${mqtt_source}")
    endif()
endforeach()
if(NOT mqtt_replacement_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one mqtt_client.c in the resolved MQTT target")
endif()
set_property(TARGET ${mqtt_component_lib} PROPERTY SOURCES "${mqtt_replaced_sources}")
target_include_directories(${mqtt_component_lib} BEFORE PRIVATE "${mqtt_generated_dir}")
