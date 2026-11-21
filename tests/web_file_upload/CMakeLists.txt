cmake_minimum_required(VERSION 3.16)
project(rodakos_web_file_upload_tests LANGUAGES CXX ASM)
enable_testing()
add_executable(rodakos_web_file_upload_tests
    web_file_upload_test.cc
    web_stdio_faults.cc
    ../../main/phone_os/web_file_system_service.cc
    ../../main/phone_os/web_access_guard.cc
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
# Same symbols the IDF build's target_add_binary_data(... TEXT) provides, from the real pages.
function(embed_text_page name)
    set(page "${CMAKE_CURRENT_SOURCE_DIR}/../../main/phone_os/${name}.html")
    set(asm "${CMAKE_CURRENT_BINARY_DIR}/${name}.S")
    file(WRITE "${asm}"
        ".section .rodata
.global _binary_${name}_html_start
.global _binary_${name}_html_end
"
        "_binary_${name}_html_start:
.incbin \"${page}\"
.byte 0
_binary_${name}_html_end:
"
        ".section .note.GNU-stack,\"\",@progbits
")
    set_property(SOURCE "${asm}" APPEND PROPERTY OBJECT_DEPENDS "${page}")
    target_sources(rodakos_web_file_upload_tests PRIVATE "${asm}")
endfunction()
embed_text_page(web_files_page)
embed_text_page(camera_chart_page)
add_test(NAME rodakos_web_file_upload COMMAND rodakos_web_file_upload_tests)
set_tests_properties(rodakos_web_file_upload PROPERTIES TIMEOUT 10)
