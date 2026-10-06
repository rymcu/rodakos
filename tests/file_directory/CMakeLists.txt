cmake_minimum_required(VERSION 3.16)
project(rodakos_file_directory_tests LANGUAGES CXX)
enable_testing()

add_executable(rodakos_file_directory_tests
    file_directory_test.cc
    ../app_model/test_main.cc
    ../../main/rodakos_adapters/file_directory.cc
)
target_include_directories(rodakos_file_directory_tests PRIVATE ../app_model ../../main)
target_compile_features(rodakos_file_directory_tests PRIVATE cxx_std_17)
target_compile_options(rodakos_file_directory_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(rodakos_file_directory_tests PRIVATE
    -Wl,--wrap=opendir -Wl,--wrap=readdir -Wl,--wrap=stat -Wl,--wrap=closedir
)
add_test(NAME rodakos_file_directory COMMAND rodakos_file_directory_tests)
set_tests_properties(rodakos_file_directory PROPERTIES TIMEOUT 20)
