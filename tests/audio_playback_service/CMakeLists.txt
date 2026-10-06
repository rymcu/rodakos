cmake_minimum_required(VERSION 3.16)
project(rodakos_audio_playback_service_tests LANGUAGES C CXX)
enable_testing()
find_package(Threads REQUIRED)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(HELIX_ROOT "${RODAKOS_ROOT}/managed_components/chmorgan__esp-libhelix-mp3/libhelix-mp3")
file(GLOB HELIX_SOURCES "${HELIX_ROOT}/*.c" "${HELIX_ROOT}/real/*.c")
add_library(audio_test_helix STATIC ${HELIX_SOURCES})
target_include_directories(audio_test_helix PRIVATE "${HELIX_ROOT}/pub" "${HELIX_ROOT}/real")
target_compile_options(audio_test_helix PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/helix_host_arithmetic.h")
add_executable(rodakos_audio_playback_service_tests
    audio_playback_test.cc host_runtime.cc stdio_faults.cc
    "${RODAKOS_ROOT}/tests/app_model/test_main.cc"
    "${RODAKOS_ROOT}/main/phone_os/audio_service.cc")
target_include_directories(rodakos_audio_playback_service_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes" "${HELIX_ROOT}/pub"
    "${RODAKOS_ROOT}/tests/app_model/fakes" "${RODAKOS_ROOT}/tests/app_model" "${RODAKOS_ROOT}/main")
target_compile_features(rodakos_audio_playback_service_tests PRIVATE cxx_std_17)
target_compile_options(rodakos_audio_playback_service_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(rodakos_audio_playback_service_tests PRIVATE audio_test_helix Threads::Threads)
target_link_options(rodakos_audio_playback_service_tests PRIVATE -Wl,--wrap=fread -Wl,--wrap=ferror -Wl,--wrap=fclose)
add_test(NAME rodakos_audio_playback_service COMMAND rodakos_audio_playback_service_tests)
set_tests_properties(rodakos_audio_playback_service PROPERTIES TIMEOUT 40)
