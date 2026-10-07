cmake_minimum_required(VERSION 3.20)
project(rodakos_navigation_ui_tests LANGUAGES C CXX)
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
add_executable(rodakos_navigation_ui_tests
    navigation_ui_test.cc
    "${RODAKOS_ROOT}/tests/home_ui/test_main.cc"
    "${RODAKOS_ROOT}/tests/home_ui/fakes/test_fonts.cc"
    "${RODAKOS_ROOT}/main/apps/camera/camera_app.cc"
    "${RODAKOS_ROOT}/main/phone_os/phone_app_registry.cc"
    "${RODAKOS_ROOT}/main/phone_os/phone_app_host.cc"
    "${RODAKOS_ROOT}/main/phone_os/phone_system.cc"
    "${RODAKOS_ROOT}/main/phone_os/phone_navigation.cc"
    "${RODAKOS_ROOT}/main/phone_os/deferred_navigation.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_ui.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_components.cc"
    "${RODAKOS_ROOT}/main/phone_ui/phone_theme.cc"
    "${RODAKOS_ROOT}/main/phone_ui/rodakos_theme.cc"
)
target_include_directories(rodakos_navigation_ui_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${RODAKOS_ROOT}/tests/camera_ui/fakes"
    "${RODAKOS_ROOT}/tests/home_ui/fakes"
    "${RODAKOS_ROOT}/tests/home_ui"
    "${RODAKOS_ROOT}/main"
    "${RODAKOS_ROOT}/components/rodakos_fonts/include"
)
find_package(Threads REQUIRED)
target_link_libraries(rodakos_navigation_ui_tests PRIVATE lvgl Threads::Threads m)
target_compile_options(rodakos_navigation_ui_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME rodakos_navigation_ui COMMAND rodakos_navigation_ui_tests)
set_tests_properties(rodakos_navigation_ui PROPERTIES TIMEOUT 60)
