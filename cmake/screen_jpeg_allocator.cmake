# project() 已完成组件解析；最终 ELF 校验是正常构建的强制步骤。
idf_build_get_property(jpeg_audit_python PYTHON)
idf_build_get_property(jpeg_audit_elf EXECUTABLE)
idf_build_get_property(jpeg_audit_sdkconfig SDKCONFIG)
set(jpeg_audit_script "${PROJECT_DIR}/tools/check_screen_jpeg_allocator.py")
set(jpeg_audit_component "${PROJECT_DIR}/managed_components/espressif__esp_new_jpeg")
set(jpeg_audit_inputs
    "${jpeg_audit_script}"
    "${PROJECT_DIR}/main/idf_component.yml"
    "${PROJECT_DIR}/dependencies.lock"
    "${jpeg_audit_sdkconfig}"
    "${jpeg_audit_component}/lib/esp32s3/libesp_new_jpeg.a"
    "${jpeg_audit_component}/include/esp_jpeg_common.h"
    "${jpeg_audit_component}/include/esp_jpeg_enc.h"
    "${jpeg_audit_component}/idf_component.yml"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${jpeg_audit_inputs})
string(REGEX REPLACE "objdump(\\.exe)?$" "" jpeg_audit_prefix "${CMAKE_OBJDUMP}")
if(jpeg_audit_prefix STREQUAL CMAKE_OBJDUMP)
    message(FATAL_ERROR "Cannot derive JPEG audit tool prefix from CMAKE_OBJDUMP")
endif()
set(jpeg_audit_command
    "${jpeg_audit_python}" "${jpeg_audit_script}"
    --repo "${PROJECT_DIR}" --tool-prefix "${jpeg_audit_prefix}"
    --target "${IDF_TARGET}"
    --idf-version "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}"
    --sdkconfig "${jpeg_audit_sdkconfig}"
)
execute_process(COMMAND ${jpeg_audit_command} --output "${CMAKE_BINARY_DIR}/screen-jpeg-preflight.json"
    RESULT_VARIABLE jpeg_audit_result OUTPUT_VARIABLE jpeg_audit_output ERROR_VARIABLE jpeg_audit_error)
if(NOT jpeg_audit_result EQUAL 0)
    message(FATAL_ERROR "JPEG allocator ABI refused: ${jpeg_audit_output}${jpeg_audit_error}")
endif()
set(RODAKOS_JPEG_BASELINE_ELF "" CACHE FILEPATH "Optional prior ELF for per-task TLS delta evidence")
set(jpeg_audit_baseline_args "")
if(RODAKOS_JPEG_BASELINE_ELF)
    list(APPEND jpeg_audit_baseline_args --baseline-elf "${RODAKOS_JPEG_BASELINE_ELF}")
    list(APPEND jpeg_audit_inputs "${RODAKOS_JPEG_BASELINE_ELF}")
endif()
set_property(TARGET ${jpeg_audit_elf} APPEND PROPERTY LINK_DEPENDS ${jpeg_audit_inputs})
add_custom_command(TARGET ${jpeg_audit_elf} POST_BUILD
    COMMAND ${jpeg_audit_command}
        --elf "$<TARGET_FILE:${jpeg_audit_elf}>"
        --map "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map"
        ${jpeg_audit_baseline_args}
        --output "${CMAKE_BINARY_DIR}/screen-jpeg-linked.json"
    COMMENT "Verify scoped JPEG allocation calls, archive ABI and native TLS"
    VERBATIM
)
