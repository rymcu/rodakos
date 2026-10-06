cmake_minimum_required(VERSION 3.16)
project(rodakos_web_file_upload_tests LANGUAGES CXX)
enable_testing()
add_executable(rodakos_web_file_upload_tests
    web_file_upload_test.cc
    web_stdio_faults.cc
    ../../main/phone_os/web_file_system_service.cc
    ../../main/rodakos_adapters/file_path_lease.cc
    ../app_model/test_main.cc)
target_include_directories(rodakos_web_file_upload_tests BEFORE PRIVATE fakes ../app_model ../../main)
target_compile_features(rodakos_web_file_upload_tests PRIVATE cxx_std_17)
target_compile_options(rodakos_web_file_upload_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
find_package(Threads REQUIRED)
target_link_libraries(rodakos_web_file_upload_tests PRIVATE Threads::Threads)
target_link_options(rodakos_web_file_upload_tests PRIVATE
    -Wl,--wrap=fopen -Wl,--wrap=fwrite -Wl,--wrap=ferror -Wl,--wrap=fflush -Wl,--wrap=fclose -Wl,--wrap=remove
)
add_test(NAME rodakos_web_file_upload COMMAND rodakos_web_file_upload_tests)
set_tests_properties(rodakos_web_file_upload PROPERTIES TIMEOUT 10)
