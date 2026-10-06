cmake_minimum_required(VERSION 3.16)
project(rodakos_file_writer_tests LANGUAGES CXX)
enable_testing()
find_package(Threads REQUIRED)
add_executable(rodakos_file_writer_tests
    file_writer_test.cc ../app_model/test_main.cc
    ../../main/rodakos_adapters/file_writer.cc
)
target_include_directories(rodakos_file_writer_tests PRIVATE ../app_model ../../main)
target_compile_features(rodakos_file_writer_tests PRIVATE cxx_std_17)
target_compile_options(rodakos_file_writer_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rodakos_file_writer_tests PRIVATE Threads::Threads)
target_link_options(rodakos_file_writer_tests PRIVATE
    -Wl,--wrap=open -Wl,--wrap=fdopen -Wl,--wrap=fopen -Wl,--wrap=fwrite
    -Wl,--wrap=ferror -Wl,--wrap=fflush -Wl,--wrap=fclose -Wl,--wrap=close -Wl,--wrap=unlink
)
add_test(NAME rodakos_file_writer COMMAND rodakos_file_writer_tests)
set_tests_properties(rodakos_file_writer PROPERTIES TIMEOUT 20)
