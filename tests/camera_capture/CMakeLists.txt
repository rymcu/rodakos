cmake_minimum_required(VERSION 3.16)
project(rodakos_camera_capture_tests LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(rodakos_camera_capture_tests
    camera_capture_test.cc host_runtime.cc
    ../app_model/test_main.cc
    ../../main/phone_os/camera_service.cc
    ../../main/rodakos_adapters/file_service.cc
    ../../main/rodakos_adapters/file_directory.cc
    ../../main/rodakos_adapters/file_path_lease.cc
    ../../main/rodakos_adapters/file_writer.cc
)
target_include_directories(rodakos_camera_capture_tests BEFORE PRIVATE
    fakes ../app_model/fakes ../app_model ../../main)
target_compile_features(rodakos_camera_capture_tests PRIVATE cxx_std_17)
target_compile_options(rodakos_camera_capture_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(rodakos_camera_capture_tests PRIVATE Threads::Threads)
target_link_options(rodakos_camera_capture_tests PRIVATE
    -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=ioctl -Wl,--wrap=mmap -Wl,--wrap=munmap
    -Wl,--wrap=time -Wl,--wrap=mkdir -Wl,--wrap=fwrite -Wl,--wrap=fflush -Wl,--wrap=fclose)
add_test(NAME rodakos_camera_capture COMMAND rodakos_camera_capture_tests)
set_tests_properties(rodakos_camera_capture PROPERTIES TIMEOUT 30)
