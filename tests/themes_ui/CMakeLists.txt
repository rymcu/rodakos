cmake_minimum_required(VERSION 3.20)
project(rodakos_themes_ui_tests LANGUAGES C CXX)
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
# Production fonts (Montserrat -> Ionicons -> PuHui) so screenshots show real Chinese and icons.
add_executable(rodakos_themes_ui_tests
    themes_ui_test.cc
    "${RODAKOS_ROOT}/tests/home_ui/test_main.cc"
    "${RODAKOS_ROOT}/main/apps/themes/themes_app.cc"
    "${RODAKOS_ROOT}/main/phone_os/phone_app_registry.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_ui.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_components.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_fonts.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_theme.cc"
    "${RODAKOS_ROOT}/main/phone_ui/rodakos_theme.cc"
    "${RODAKOS_ROOT}/main/phone_ui/theme_presets.cc"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/font_puhui_16_4.c"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/rodakos_icons_20_4.c"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/rodakos_icons_30_4.c"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/font_edix_36_4.c"
)
target_include_directories(rodakos_themes_ui_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${RODAKOS_ROOT}/tests/home_ui/fakes"
    "${RODAKOS_ROOT}/tests/home_ui"
    "${RODAKOS_ROOT}/main"
    "${RODAKOS_ROOT}/components/rodakos_fonts/include"
)
target_compile_definitions(rodakos_themes_ui_tests PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
find_package(Threads REQUIRED)
target_link_libraries(rodakos_themes_ui_tests PRIVATE lvgl Threads::Threads m)
target_compile_options(rodakos_themes_ui_tests PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>)
add_test(NAME rodakos_themes_ui COMMAND rodakos_themes_ui_tests)
set_tests_properties(rodakos_themes_ui PROPERTIES TIMEOUT 60)
