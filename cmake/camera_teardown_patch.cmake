# Resolve and validate all inputs before replacing either managed translation unit.
option(RODAKOS_CAMERA_DMA_FORCE_4096
    "Test only: skip the preferred 6144-byte non-JPEG DVP DMA ring" OFF)
option(RODAKOS_CAMERA_DMA_FAIL_FIRST_RING
    "Test only: fail the first non-JPEG DVP DMA ring allocation" OFF)

if(RODAKOS_CAMERA_DMA_FORCE_4096 AND RODAKOS_CAMERA_DMA_FAIL_FIRST_RING)
    message(FATAL_ERROR "Camera DMA fault injections are mutually exclusive")
endif()

idf_component_get_property(camera_video_dir espressif__esp_video COMPONENT_DIR)
idf_component_get_property(camera_video_lib espressif__esp_video COMPONENT_LIB)
idf_component_get_property(camera_sensor_dir espressif__esp_cam_sensor COMPONENT_DIR)
idf_component_get_property(camera_sensor_lib espressif__esp_cam_sensor COMPONENT_LIB)
idf_build_get_property(camera_patch_python PYTHON)
idf_build_get_property(camera_patch_idf_path IDF_PATH)
set(camera_patch_script "${PROJECT_DIR}/tools/prepare_camera_teardown_patch.py")
set(camera_generated_dir "${CMAKE_BINARY_DIR}/rodak_patches/camera_teardown")
set(camera_generated_video "${camera_generated_dir}/esp_video_device_common.c")
set(camera_generated_sensor "${camera_generated_dir}/esp_cam_ctlr_dvp_cam.c")
set(camera_patch_command
    "${camera_patch_python}" "${camera_patch_script}"
    --video-dir "${camera_video_dir}"
    --sensor-dir "${camera_sensor_dir}"
    --lock-file "${PROJECT_DIR}/dependencies.lock"
    --project-manifest "${PROJECT_DIR}/main/idf_component.yml"
    --idf-path "${camera_patch_idf_path}"
    --diagnostics-header "${PROJECT_DIR}/main/phone_os/camera-teardown-diagnostics.h"
    --output-dir "${camera_generated_dir}"
)
execute_process(COMMAND ${camera_patch_command} --print-inputs
    RESULT_VARIABLE camera_inputs_result OUTPUT_VARIABLE camera_inputs_output ERROR_VARIABLE camera_inputs_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT camera_inputs_result EQUAL 0)
    message(FATAL_ERROR "${camera_inputs_output}${camera_inputs_error}")
endif()
string(REPLACE "\r\n" "\n" camera_inputs_output "${camera_inputs_output}")
string(REPLACE "\n" ";" camera_patch_inputs "${camera_inputs_output}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${camera_patch_inputs})
execute_process(COMMAND ${camera_patch_command}
    RESULT_VARIABLE camera_patch_result OUTPUT_VARIABLE camera_patch_output ERROR_VARIABLE camera_patch_error)
if(NOT camera_patch_result EQUAL 0)
    message(FATAL_ERROR "${camera_patch_output}${camera_patch_error}")
endif()
string(STRIP "${camera_patch_output}" camera_patch_output)
message(STATUS "${camera_patch_output}")
add_custom_command(OUTPUT "${camera_generated_video}" "${camera_generated_sensor}"
    COMMAND ${camera_patch_command} DEPENDS ${camera_patch_inputs} VERBATIM)
add_custom_target(rodak_camera_teardown_overlay DEPENDS "${camera_generated_video}" "${camera_generated_sensor}")

function(rodak_replace_camera_source component_lib component_dir original_source generated_source)
    get_target_property(camera_sources ${component_lib} SOURCES)
    set(camera_replaced_sources "")
    set(camera_replacement_count 0)
    foreach(camera_source IN LISTS camera_sources)
        get_filename_component(camera_source_absolute "${camera_source}" ABSOLUTE BASE_DIR "${component_dir}")
        if(camera_source_absolute STREQUAL original_source)
            list(APPEND camera_replaced_sources "${generated_source}")
            math(EXPR camera_replacement_count "${camera_replacement_count} + 1")
        else()
            list(APPEND camera_replaced_sources "${camera_source}")
        endif()
    endforeach()
    if(NOT camera_replacement_count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one reviewed camera source in ${component_lib}: ${original_source}")
    endif()
    set_property(TARGET ${component_lib} PROPERTY SOURCES "${camera_replaced_sources}")
    add_dependencies(${component_lib} rodak_camera_teardown_overlay)
    # A private C ABI header introduces no reverse component dependency on main.
    target_include_directories(${component_lib} PRIVATE "${PROJECT_DIR}/main/phone_os")
endfunction()
rodak_replace_camera_source("${camera_video_lib}" "${camera_video_dir}"
    "${camera_video_dir}/src/device/esp_video_device_common.c" "${camera_generated_video}")
rodak_replace_camera_source("${camera_sensor_lib}" "${camera_sensor_dir}"
    "${camera_sensor_dir}/src/driver_dvp/esp_cam_ctlr_dvp_cam.c" "${camera_generated_sensor}")
if(RODAKOS_CAMERA_DMA_FORCE_4096)
    target_compile_definitions(${camera_sensor_lib} PRIVATE RODAKOS_CAMERA_DMA_FORCE_4096=1)
    message(WARNING "Camera DMA 4096-byte fault injection is active; never ship this build")
endif()
if(RODAKOS_CAMERA_DMA_FAIL_FIRST_RING)
    target_compile_definitions(${camera_sensor_lib}
        PRIVATE RODAKOS_CAMERA_DMA_FAIL_FIRST_RING=1)
    message(WARNING "Camera first-ring failure injection is active; never ship this build")
endif()
