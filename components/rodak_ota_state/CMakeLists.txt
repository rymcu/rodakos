idf_component_register(
    SRCS "rodak_ota_state.cc" "rodak_ota_signature.cc" "rodak_ota_signature_format.cc"
        "rodak_release_fault.cc"
    INCLUDE_DIRS "include"
    REQUIRES mbedtls nvs_flash esp_system
)

set(RODAK_OTA_FAULT_INJECTION_PHASE "" CACHE STRING "One-shot OTA reset boundary")
set(RODAK_RELEASE_TEST_ID "trial-1" CACHE STRING "Change to re-arm a release fault test")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT RODAK_RELEASE_TEST_ID MATCHES "^[a-zA-Z0-9_-]+$")
        message(FATAL_ERROR "RODAK_RELEASE_TEST_ID must contain only letters, digits, _ or -")
    endif()
    set(fault_points after_download_fsync after_pending_sidecar after_pending_journal
        after_restore_state after_restore_write after_rollback_ready after_applying_state
        after_image_write after_ready_to_boot after_boot_confirmation
        before_image_erase after_image_erase during_image_write
        before_restore_erase after_restore_erase during_restore_write
        before_journal_set after_journal_set after_journal_commit
        after_result_http_ack after_report_acknowledged)
    if(RODAK_OTA_FAULT_INJECTION_PHASE AND NOT RODAK_OTA_FAULT_INJECTION_PHASE IN_LIST fault_points)
        message(FATAL_ERROR "Unknown OTA fault injection boundary")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        RODAK_OTA_FAULT_INJECTION_PHASE="${RODAK_OTA_FAULT_INJECTION_PHASE}"
        RODAK_RELEASE_TEST_ID="${RODAK_RELEASE_TEST_ID}")
endif()

set(RODAK_OTA_PUBLIC_KEY "" CACHE FILEPATH "RSA-2048 public verification key (PEM)")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(trust_header "${CMAKE_CURRENT_BINARY_DIR}/rodak_ota_trust.h")
    execute_process(
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../tools/ota_security.py"
            key-header --public-key "${RODAK_OTA_PUBLIC_KEY}" --output "${trust_header}"
        RESULT_VARIABLE trust_result
    )
    if(NOT trust_result EQUAL 0)
        message(FATAL_ERROR "Unable to configure OTA public verification key")
    endif()
    if(RODAK_OTA_PUBLIC_KEY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RODAK_OTA_PUBLIC_KEY}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../../tools/ota_security.py")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
