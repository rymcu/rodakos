cmake_minimum_required(VERSION 3.20)

project(rodakos_site_screenshots LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LVGL_ROOT "${RODAKOS_ROOT}/managed_components/lvgl__lvgl")
set(HOME_UI_TESTS "${RODAKOS_ROOT}/tests/home_ui")
set(ICON_PREVIEW "${RODAKOS_ROOT}/tools/icon_preview")

set(LV_BUILD_CONF_PATH "${ICON_PREVIEW}/lv_conf.h" CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")

add_executable(rodakos_site_screenshots
    render_site_screens.cc
    "${RODAKOS_ROOT}/main/apps/home/home_app.cc"
    "${RODAKOS_ROOT}/main/apps/home/home_layout_model.cc"
    "${RODAKOS_ROOT}/main/apps/home/home_layout_codec.cc"
    "${RODAKOS_ROOT}/main/apps/home/home_layout_store.cc"
    "${RODAKOS_ROOT}/main/phone_os/phone_app_registry.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_components.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_theme.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_ui.cc"
    "${RODAKOS_ROOT}/main/phone_ui/rodakos_layout.cc"
    "${RODAKOS_ROOT}/main/phone_ui/rodakos_theme.cc"
    "${RODAKOS_ROOT}/main/phone_ui/soft_keyboard.cc"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/rodakos_icons_20_4.c"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/rodakos_icons_30_4.c"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/font_puhui_16_4.c"
    "${RODAKOS_ROOT}/components/rodakos_fonts/src/font_edix_36_4.c"
    "${RODAKOS_ROOT}/tests/app_model/third_party/cjson/cJSON.c"
)

target_include_directories(rodakos_site_screenshots BEFORE PRIVATE
    "${ICON_PREVIEW}/fakes"
    "${HOME_UI_TESTS}/fakes"
    "${RODAKOS_ROOT}/main"
    "${RODAKOS_ROOT}/tests/app_model/third_party/cjson"
    "${RODAKOS_ROOT}/components/rodakos_fonts/include"
)

target_link_libraries(rodakos_site_screenshots PRIVATE lvgl)
if(UNIX)
    target_link_libraries(rodakos_site_screenshots PRIVATE m)
endif()
