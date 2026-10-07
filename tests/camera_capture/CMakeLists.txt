cmake_minimum_required(VERSION 3.16)
project(rodakos_camera_capture_tests LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CAMERA_HOST_SOURCES
    host_runtime.cc
    ../../main/phone_os/camera_service.cc
    ../../main/phone_os/camera-teardown-diagnostics.cc
    ../../main/rodakos_adapters/file_service.cc
    ../../main/rodakos_adapters/file_directory.cc
    ../../main/rodakos_adapters/file_path_lease.cc
    ../../main/rodakos_adapters/file_writer.cc
)
function(configure_camera_host_target target)
target_include_directories(${target} BEFORE PRIVATE
    fakes . ../app_model/fakes ../app_model ../../main)
target_compile_features(${target} PRIVATE cxx_std_17)
target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
target_link_libraries(${target} PRIVATE Threads::Threads)
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
