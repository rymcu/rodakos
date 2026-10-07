cmake_minimum_required(VERSION 3.20)
project(rodakos_camera_worker_lifecycle_tests LANGUAGES C CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RODAKOS_IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "Reviewed ESP-IDF 6.0.2")
set(RODAK_WORKER_SOURCE "" CACHE FILEPATH "Frozen reviewed source for baseline or negative controls")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract_worker.py"
    --idf-path "${RODAKOS_IDF_PATH}" --output-dir "${generated}" --source "${RODAK_WORKER_SOURCE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${output}${error}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${RODAKOS_ROOT}/tools/prepare_camera_teardown_patch.py"
    "${RODAKOS_ROOT}/patches/camera_teardown/2.3.0/provenance.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/extract_worker.py")
add_executable(rodakos_camera_worker_lifecycle_tests worker_test.cc host_runtime.cc
    "${generated}/production_worker.c" "${RODAKOS_ROOT}/tests/display_control_ack_service/test_main.cc")
target_compile_features(rodakos_camera_worker_lifecycle_tests PRIVATE c_std_11 cxx_std_20)
target_include_directories(rodakos_camera_worker_lifecycle_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${generated}" "${RODAKOS_ROOT}/tests/app_model"
    "${RODAKOS_ROOT}/main/phone_os")
target_link_libraries(rodakos_camera_worker_lifecycle_tests PRIVATE Threads::Threads)
target_compile_options(rodakos_camera_worker_lifecycle_tests PRIVATE
    -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -fexceptions)
add_test(NAME camera_worker_lifecycle COMMAND rodakos_camera_worker_lifecycle_tests)
set_tests_properties(camera_worker_lifecycle PROPERTIES TIMEOUT 30)
if(NOT RODAK_WORKER_SOURCE)
    add_test(NAME camera_worker_negative_controls
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_negative.py"
        --source "${generated}/esp_cam_ctlr_dvp_cam.c"
        --output "${CMAKE_CURRENT_BINARY_DIR}/negative-controls"
        --idf-path "${RODAKOS_IDF_PATH}"
        --c-flags=${CMAKE_C_FLAGS} --cxx-flags=${CMAKE_CXX_FLAGS}
        --link-flags=${CMAKE_EXE_LINKER_FLAGS})
    set_tests_properties(camera_worker_negative_controls PROPERTIES TIMEOUT 180)
endif()
