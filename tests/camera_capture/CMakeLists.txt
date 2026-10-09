cmake_minimum_required(VERSION 3.16)
project(rodakos_camera_capture_tests LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT TARGET rodakos_task_retirement_host)
    add_subdirectory(../task_retirement ${CMAKE_CURRENT_BINARY_DIR}/task_retirement)
endif()
# The negative control supplies frozen full production files from the baseline
# commit. The host runtime and pinned real IDF implementation remain identical.
set(CAMERA_SERVICE_SOURCE "${RODAKOS_ROOT}/main/phone_os/camera_service.cc" CACHE FILEPATH
    "Full production camera service translation unit")
set(CAMERA_SERVICE_INCLUDE_ROOT "${RODAKOS_ROOT}/main" CACHE PATH
    "Include root for the production camera service header")
set(CAMERA_HOST_SOURCES
    host_runtime.cc
    ${CAMERA_SERVICE_SOURCE}
    ../../main/phone_os/camera-teardown-diagnostics.cc
    ../../main/rodakos_adapters/file_service.cc
    ../../main/rodakos_adapters/file_directory.cc
    ../../main/rodakos_adapters/file_path_lease.cc
    ../../main/rodakos_adapters/file_writer.cc
)
function(configure_camera_host_target target)
target_include_directories(${target} BEFORE PRIVATE
    ../task_retirement/fakes fakes . ../app_model/fakes ../app_model
    ${CAMERA_SERVICE_INCLUDE_ROOT} ../../main)
target_compile_features(${target} PRIVATE cxx_std_17)
target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
target_link_libraries(${target} PRIVATE Threads::Threads rodakos_task_retirement_host)
target_link_options(${target} PRIVATE
    # Wrap allocations inside std::string/std::function as well as inline vector
    # code; dynamic libstdc++ calls would bypass the link-time wrappers.
    -static-libstdc++
    -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=ioctl -Wl,--wrap=mmap -Wl,--wrap=munmap
    -Wl,--wrap=_Znwm -Wl,--wrap=_ZdlPv -Wl,--wrap=_ZdlPvm
    -Wl,--wrap=time -Wl,--wrap=mkdir -Wl,--wrap=fwrite -Wl,--wrap=fflush -Wl,--wrap=fclose)
endfunction()
add_executable(rodakos_camera_capture_tests
    camera_capture_test.cc ../app_model/test_main.cc ${CAMERA_HOST_SOURCES})
configure_camera_host_target(rodakos_camera_capture_tests)
add_test(NAME rodakos_camera_capture COMMAND rodakos_camera_capture_tests)
set_tests_properties(rodakos_camera_capture PROPERTIES TIMEOUT 30)

add_executable(rodakos_camera_close_diagnostics_tests
    camera_close_diagnostics_test.cc ${CAMERA_HOST_SOURCES})
configure_camera_host_target(rodakos_camera_close_diagnostics_tests)
# The production recorder has no reset API: each case starts a fresh process.
foreach(stage ioctl log)
    foreach(status 0 -1)
        set(name rodakos_camera_close_${stage}_${status})
        add_test(NAME ${name} COMMAND rodakos_camera_close_diagnostics_tests ${stage} ${status})
        set_tests_properties(${name} PROPERTIES TIMEOUT 15)
    endforeach()
endforeach()

add_executable(rodakos_camera_retirement_probe camera_retirement_probe.cc ${CAMERA_HOST_SOURCES})
configure_camera_host_target(rodakos_camera_retirement_probe)
foreach(worker preview jpeg)
    add_test(NAME rodakos_camera_retirement_${worker}
        COMMAND rodakos_camera_retirement_probe ${worker})
    set_tests_properties(rodakos_camera_retirement_${worker} PROPERTIES TIMEOUT 15)
endforeach()

add_executable(rodakos_camera_test_pattern_probe camera_test_pattern_probe.cc ${CAMERA_HOST_SOURCES})
configure_camera_host_target(rodakos_camera_test_pattern_probe)
target_compile_definitions(rodakos_camera_test_pattern_probe PRIVATE RODAKOS_CAMERA_TEST_PATTERN=1)
add_test(NAME rodakos_camera_test_pattern COMMAND rodakos_camera_test_pattern_probe)
set_tests_properties(rodakos_camera_test_pattern PROPERTIES TIMEOUT 15)

add_executable(rodakos_camera_sensor_diagnostics_probe
    camera_sensor_diagnostics_probe.cc ${CAMERA_HOST_SOURCES})
configure_camera_host_target(rodakos_camera_sensor_diagnostics_probe)
target_compile_definitions(
    rodakos_camera_sensor_diagnostics_probe PRIVATE RODAKOS_CAMERA_SENSOR_DIAGNOSTICS=1)
add_test(NAME rodakos_camera_sensor_diagnostics COMMAND rodakos_camera_sensor_diagnostics_probe)
set_tests_properties(rodakos_camera_sensor_diagnostics PROPERTIES TIMEOUT 15)
