cmake_minimum_required(VERSION 3.16)
project(rodakos_recording_service_tests LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(rodakos_recording_service_tests
    recording_service_test.cc stdio_faults.cc
    "${RODAKOS_ROOT}/tests/audio_playback_service/host_runtime.cc"
    "${RODAKOS_ROOT}/tests/app_model/test_main.cc"
    "${RODAKOS_ROOT}/main/phone_os/recording_service.cc")
target_include_directories(rodakos_recording_service_tests BEFORE PRIVATE
    fakes "${RODAKOS_ROOT}/tests/audio_playback_service/fakes"
    "${RODAKOS_ROOT}/tests/audio_playback_service" "${RODAKOS_ROOT}/tests/app_model/fakes"
    "${RODAKOS_ROOT}/tests/app_model" "${RODAKOS_ROOT}/main")
target_compile_features(rodakos_recording_service_tests PRIVATE cxx_std_17)
target_compile_options(rodakos_recording_service_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(rodakos_recording_service_tests PRIVATE Threads::Threads)
target_link_options(rodakos_recording_service_tests PRIVATE
    -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=fdopen -Wl,--wrap=fopen
    -Wl,--wrap=fwrite -Wl,--wrap=fread -Wl,--wrap=ferror -Wl,--wrap=fseek
    -Wl,--wrap=fflush -Wl,--wrap=fclose -Wl,--wrap=remove -Wl,--wrap=time)
add_test(NAME rodakos_recording_service COMMAND rodakos_recording_service_tests)
set_tests_properties(rodakos_recording_service PROPERTIES TIMEOUT 40)
