cmake_minimum_required(VERSION 3.16)
project(rodakos_setup_qr_decoder_tests LANGUAGES C CXX)
enable_testing()

add_executable(setup_qr_decoder_tests
    ../app_model/test_main.cc
    setup_qr_decoder_test.cc
    ../../main/phone_os/setup_qr_decoder.cc
    ../../main/phone_os/setup_qr_payload.cc
    ../../main/phone_os/serial_provisioning_protocol.cc
    ../../components/quirc/lib/decode.c
    ../../components/quirc/lib/identify.c
    ../../components/quirc/lib/quirc.c
    ../../components/quirc/lib/version_db.c
)
target_include_directories(setup_qr_decoder_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../app_model"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../main"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/quirc/lib"
)
# Match components/quirc/CMakeLists.txt so the host decodes with the firmware's float settings.
set_source_files_properties(
    ../../components/quirc/lib/decode.c
    ../../components/quirc/lib/identify.c
    ../../components/quirc/lib/quirc.c
    ../../components/quirc/lib/version_db.c
    PROPERTIES COMPILE_DEFINITIONS "QUIRC_FLOAT_TYPE=float;QUIRC_USE_TGMATH")
# quirc_resize copies `min(old, new)` bytes from the old image, which is NULL with size 0 on the
# first resize. That memcpy is harmless; keep the vendored source unchanged and only stop UBSan
# from flagging it, so every other UBSan check still covers quirc.
if(CMAKE_C_FLAGS MATCHES "undefined" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(../../components/quirc/lib/quirc.c
        PROPERTIES COMPILE_OPTIONS "-fno-sanitize=nonnull-attribute")
endif()
target_compile_definitions(setup_qr_decoder_tests PRIVATE
    "RODAK_SETUP_QR_FIXTURES=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures\"")
target_compile_features(setup_qr_decoder_tests PRIVATE cxx_std_17)
if(UNIX)
    target_link_libraries(setup_qr_decoder_tests PRIVATE m)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(setup_qr_decoder_tests PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>)
endif()
add_test(NAME rodakos_setup_qr_decoder COMMAND setup_qr_decoder_tests)
