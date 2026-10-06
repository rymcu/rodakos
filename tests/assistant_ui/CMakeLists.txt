cmake_minimum_required(VERSION 3.20)
project(rodakos_assistant_ui_tests LANGUAGES C CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 20)
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${RODAKOS_ROOT}/tests/home_ui/lv_conf.h" CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${RODAKOS_ROOT}/managed_components/lvgl__lvgl" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
add_executable(assistant_ui_tests
    assistant_ui_test.cc
    settings_cloud_ui_test.cc
    settings_other_pages.cc
    fake_cloud.cc
    "${RODAKOS_ROOT}/tests/home_ui/test_main.cc"
    "${RODAKOS_ROOT}/tests/home_ui/fakes/test_fonts.cc"
    "${RODAKOS_ROOT}/main/apps/assistant/assistant_app.cc"
    "${RODAKOS_ROOT}/main/apps/settings/settings_app.cc"
    "${RODAKOS_ROOT}/main/apps/settings/settings_app_internal.cc"
    "${RODAKOS_ROOT}/main/apps/settings/settings_device_cloud_page.cc"
    "${RODAKOS_ROOT}/main/phone_ui/soft_keyboard.cc"
    "${RODAKOS_ROOT}/main/phone_os/phone_app_registry.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_ui.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_components.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_theme.cc"
    "${RODAKOS_ROOT}/main/phone_ui/rodakos_theme.cc")
target_include_directories(assistant_ui_tests BEFORE PRIVATE
    fakes "${RODAKOS_ROOT}/tests/home_ui/fakes" "${RODAKOS_ROOT}/tests/home_ui"
    "${RODAKOS_ROOT}/main" "${RODAKOS_ROOT}/components/rodakos_fonts/include")
find_package(Threads REQUIRED)
target_link_libraries(assistant_ui_tests PRIVATE lvgl Threads::Threads m)
target_link_options(assistant_ui_tests PRIVATE -Wl,--wrap=lv_async_call)
target_compile_options(assistant_ui_tests PRIVATE -Wall -Wextra -Wpedantic)
set_source_files_properties(settings_cloud_ui_test.cc PROPERTIES COMPILE_OPTIONS -fno-access-control)
add_test(NAME assistant_ui COMMAND assistant_ui_tests)
set_tests_properties(assistant_ui PROPERTIES TIMEOUT 60)
