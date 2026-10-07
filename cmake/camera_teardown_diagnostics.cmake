# IDF's SPIRAM workaround routes generic atomics through libc. IDF itself opts
# stdatomic_s32c1i.c back into hardware atomics for non-external addresses (see
# esp_libc/priv_include/esp_stdatomic.h). This TU only atomically accesses its
# private DRAM_ATTR object; retain the compile-time and final-ELF address gates.
if(NOT IDF_TARGET STREQUAL "esp32s3" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Camera teardown diagnostics require reviewed ESP32-S3 GCC code generation")
endif()
idf_component_get_property(camera_diag_main main COMPONENT_LIB)
set(camera_diag_source "${PROJECT_DIR}/main/phone_os/camera-teardown-diagnostics.cc")
set_property(SOURCE "${camera_diag_source}" TARGET_DIRECTORY ${camera_diag_main}
    APPEND PROPERTY COMPILE_OPTIONS "-mno-disable-hardware-atomics")

idf_build_get_property(camera_diag_python PYTHON)
idf_build_get_property(camera_diag_elf EXECUTABLE)
idf_build_get_property(camera_diag_sdkconfig SDKCONFIG)
set(camera_diag_script "${PROJECT_DIR}/tools/check_camera_teardown_diagnostics.py")
set(camera_diag_inputs
    "${camera_diag_script}"
    "${PROJECT_DIR}/tools/check_screen_jpeg_allocator.py"
    "${camera_diag_source}"
    "${PROJECT_DIR}/main/phone_os/camera-teardown-diagnostics.h"
    "${CMAKE_CURRENT_LIST_FILE}"
    "${camera_diag_sdkconfig}"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${camera_diag_inputs})
get_filename_component(camera_diag_tool_dir "${CMAKE_OBJDUMP}" DIRECTORY)
set(camera_diag_prefix "${camera_diag_tool_dir}/xtensa-esp32s3-elf-")
if(NOT EXISTS "${camera_diag_prefix}objdump" AND NOT EXISTS "${camera_diag_prefix}objdump.exe")
    message(FATAL_ERROR "Missing ESP32-S3 objdump for camera teardown audit")
endif()
set_property(TARGET ${camera_diag_elf} APPEND PROPERTY LINK_DEPENDS ${camera_diag_inputs})
add_custom_command(TARGET ${camera_diag_elf} POST_BUILD
    COMMAND "${camera_diag_python}" "${camera_diag_script}"
        --elf "$<TARGET_FILE:${camera_diag_elf}>"
        --sdkconfig "${camera_diag_sdkconfig}"
        --tool-prefix "${camera_diag_prefix}"
        --target "${IDF_TARGET}"
        --idf-version "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}"
        --output "${CMAKE_BINARY_DIR}/camera-teardown-linked.json"
    COMMENT "Verify camera teardown DRAM storage and bounded native-atomic recorder"
    VERBATIM
)
