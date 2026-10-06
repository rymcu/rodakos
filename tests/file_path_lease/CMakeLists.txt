cmake_minimum_required(VERSION 3.16)
project(rodakos_file_path_lease_tests LANGUAGES CXX)
enable_testing()
add_executable(rodakos_file_path_lease_tests
    file_path_lease_test.cc
    ../../main/rodakos_adapters/file_path_lease.cc
    ../app_model/test_main.cc)
target_include_directories(rodakos_file_path_lease_tests PRIVATE ../app_model ../../main)
target_compile_features(rodakos_file_path_lease_tests PRIVATE cxx_std_17)
target_compile_options(rodakos_file_path_lease_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME rodakos_file_path_lease COMMAND rodakos_file_path_lease_tests)
set_tests_properties(rodakos_file_path_lease PROPERTIES TIMEOUT 10)
