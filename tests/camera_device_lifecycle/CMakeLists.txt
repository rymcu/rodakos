cmake_minimum_required(VERSION 3.20)
project(rodakos_camera_device_lifecycle_tests LANGUAGES C CXX)

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json" "{\n  \"mode\": \"complete-camera-wrapper-and-board-device\"")
foreach(source IN ITEMS
    "components/esp_board_manager/devices/dev_camera/dev_camera.c"
    "components/esp_board_manager/src/esp_board_device.c"
    "main/rodakos_adapters/camera_device.cc")
    file(SHA256 "${RODAKOS_ROOT}/${source}" source_sha)
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json"
        ",\n  \"${source}\": \"${source_sha}\"")
endforeach()
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/production-sources.json" "\n}\n")

foreach(variant patched legacy)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_host.py"
            --source "${RODAKOS_ROOT}/components/esp_board_manager/devices/dev_camera/dev_camera.c"
            --output "${generated}/${variant}/dev_camera.c"
            --variant "${variant}"
        RESULT_VARIABLE prepared OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT prepared EQUAL 0)
        message(FATAL_ERROR "Camera wrapper preparation refused (${variant}): ${output}${error}")
    endif()
endforeach()

function(configure_camera_target target source)
    add_executable(${target} camera_device_lifecycle_test.cc fixture_runtime.cc
        "${RODAKOS_ROOT}/components/esp_board_manager/src/esp_board_device.c"
        "${source}" "${RODAKOS_ROOT}/main/rodakos_adapters/camera_device.cc")
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${RODAKOS_ROOT}/main"
        "${RODAKOS_ROOT}/components/esp_board_manager/include"
        "${RODAKOS_ROOT}/components/esp_board_manager/private_inc")
    target_compile_features(${target} PRIVATE c_std_11 cxx_std_17)
    target_compile_definitions(${target} PRIVATE _GNU_SOURCE CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT=1 CONFIG_ESP_BOARD_DEV_POWER_CTRL_SUPPORT=0)
    target_compile_options(${target} PRIVATE -UNDEBUG -Wall -Wextra -Werror -Wno-unused-function)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
endfunction()

configure_camera_target(rodakos_camera_device_lifecycle_tests
    "${generated}/patched/dev_camera.c")
configure_camera_target(rodakos_camera_device_lifecycle_legacy_tests
    "${generated}/legacy/dev_camera.c")

add_test(NAME camera_device_release_failure_retry
    COMMAND rodakos_camera_device_lifecycle_tests release_failure_retry)
add_test(NAME camera_device_invalid_handle_cleanup_retry
    COMMAND rodakos_camera_device_lifecycle_tests invalid_handle_cleanup_retry)
add_test(NAME camera_device_missing_subtype_callback
    COMMAND rodakos_camera_device_lifecycle_tests missing_subtype_callback)
add_test(NAME camera_device_legacy_negative
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
        --executable "\$<TARGET_FILE:rodakos_camera_device_lifecycle_legacy_tests>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/legacy-negative.json")
set_tests_properties(camera_device_release_failure_retry camera_device_invalid_handle_cleanup_retry
    camera_device_missing_subtype_callback PROPERTIES TIMEOUT 10)
set_tests_properties(camera_device_legacy_negative PROPERTIES TIMEOUT 15)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/prepare_host.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixture_runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/camera_device_lifecycle_test.cc"
    "${RODAKOS_ROOT}/components/esp_board_manager/src/esp_board_device.c"
    "${RODAKOS_ROOT}/components/esp_board_manager/devices/dev_camera/dev_camera.c"
    "${RODAKOS_ROOT}/main/rodakos_adapters/camera_device.cc")
