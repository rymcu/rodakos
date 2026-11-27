cmake_minimum_required(VERSION 3.20)
project(rodakos_wol_ui_tests LANGUAGES C CXX)
enable_testing()
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${RODAKOS_ROOT}/tests/home_ui/lv_conf.h" CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${RODAKOS_ROOT}/managed_components/lvgl__lvgl" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
# Production WolApp, device store, codec and packet parsing on the home_ui Settings fake (NVS),
# a scripted WakeOnLanService, the production soft keyboard and fonts.
set(TEST_CJSON_DIR "${RODAKOS_ROOT}/tests/app_model/third_party/cjson")
add_executable(rodakos_wol_ui_tests
    wol_ui_test.cc
    "${RODAKOS_ROOT}/tests/home_ui/test_main.cc"
    "${RODAKOS_ROOT}/main/apps/wol/wol_app.cc"
    "${RODAKOS_ROOT}/main/apps/wol/wol_device_store.cc"
    "${RODAKOS_ROOT}/main/apps/wol/wol_device_codec.cc"
    "${RODAKOS_ROOT}/main/phone_os/wake_on_lan_packet.cc"
    "${RODAKOS_ROOT}/main/phone_os/phone_app_registry.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_ui.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_components.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_fonts.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_theme.cc"
    "${RODAKOS_ROOT}/main/phone_ui/rodakos_theme.cc"
    "${RODAKOS_ROOT}/main/phone_ui/soft_keyboard.cc"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/font_puhui_16_4.c"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/rodakos_icons_20_4.c"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/rodakos_icons_30_4.c"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/font_edix_36_4.c"
    "${TEST_CJSON_DIR}/cJSON.c"
)
target_include_directories(rodakos_wol_ui_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${RODAKOS_ROOT}/tests/themes_ui/fakes"
    "${RODAKOS_ROOT}/tests/home_ui/fakes"
    "${RODAKOS_ROOT}/tests/home_ui"
    "${RODAKOS_ROOT}/main"
    "${RODAKOS_ROOT}/components/rodakos_fonts/include"
    "${TEST_CJSON_DIR}"
)
target_compile_definitions(rodakos_wol_ui_tests PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
find_package(Threads REQUIRED)
target_link_libraries(rodakos_wol_ui_tests PRIVATE lvgl Threads::Threads m)
target_compile_options(rodakos_wol_ui_tests PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>)
set_source_files_properties(wol_ui_test.cc PROPERTIES COMPILE_OPTIONS -fno-access-control)
add_test(NAME rodakos_wol_ui COMMAND rodakos_wol_ui_tests)
set_tests_properties(rodakos_wol_ui PROPERTIES TIMEOUT 60)
