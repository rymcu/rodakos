# project() has resolved managed components before this hook is included.
idf_component_get_property(codec_component_dir espressif__esp_codec_dev COMPONENT_DIR)
idf_component_get_property(codec_component_lib espressif__esp_codec_dev COMPONENT_LIB)
idf_build_get_property(codec_patch_python PYTHON)

set(codec_patch_script "${CMAKE_CURRENT_LIST_DIR}/../tools/prepare_codec_volume_patch.py")
set(codec_patch_dir "${CMAKE_CURRENT_LIST_DIR}/../patches/esp_codec_dev/1.5.7")
set(codec_original_source "${codec_component_dir}/esp_codec_dev.c")
set(codec_generated_source "${CMAKE_BINARY_DIR}/rodak_patches/esp_codec_dev/esp_codec_dev.c")
set(codec_patch_inputs
    "${codec_patch_script}"
    "${codec_patch_dir}/provenance.json"
    "${codec_patch_dir}/set_out_vol.c"
    "${PROJECT_DIR}/dependencies.lock"
    "${PROJECT_DIR}/main/idf_component.yml"
    "${codec_component_dir}/idf_component.yml"
    "${codec_component_dir}/.component_hash"
    "${codec_original_source}"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${codec_patch_inputs})

set(codec_patch_command
    "${codec_patch_python}" "${codec_patch_script}"
    --component-dir "${codec_component_dir}"
    --lock-file "${PROJECT_DIR}/dependencies.lock"
    --project-manifest "${PROJECT_DIR}/main/idf_component.yml"
    --output "${codec_generated_source}"
)
execute_process(
    COMMAND ${codec_patch_command}
    RESULT_VARIABLE codec_patch_result
    OUTPUT_VARIABLE codec_patch_output
    ERROR_VARIABLE codec_patch_error
)
if(NOT codec_patch_result EQUAL 0)
    message(FATAL_ERROR "${codec_patch_output}${codec_patch_error}")
endif()
string(STRIP "${codec_patch_output}" codec_patch_output)
message(STATUS "${codec_patch_output}")

# Recreate the generated source if build outputs are removed without reconfiguring.
add_custom_command(OUTPUT "${codec_generated_source}"
    COMMAND ${codec_patch_command}
    DEPENDS ${codec_patch_inputs}
    VERBATIM
)
add_custom_target(rodak_codec_volume_overlay DEPENDS "${codec_generated_source}")
add_dependencies(${codec_component_lib} rodak_codec_volume_overlay)

get_target_property(codec_sources ${codec_component_lib} SOURCES)
set(codec_replaced_sources "")
set(codec_replacement_count 0)
foreach(codec_source IN LISTS codec_sources)
    get_filename_component(codec_source_absolute "${codec_source}" ABSOLUTE
        BASE_DIR "${codec_component_dir}")
    if(codec_source_absolute STREQUAL codec_original_source)
        list(APPEND codec_replaced_sources "${codec_generated_source}")
        math(EXPR codec_replacement_count "${codec_replacement_count} + 1")
    else()
        list(APPEND codec_replaced_sources "${codec_source}")
    endif()
endforeach()
if(NOT codec_replacement_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one esp_codec_dev.c in the resolved codec target")
endif()
set_property(TARGET ${codec_component_lib} PROPERTY SOURCES "${codec_replaced_sources}")
# The copied C source still uses its upstream component's private, local header.
target_include_directories(${codec_component_lib} PRIVATE "${codec_component_dir}")
