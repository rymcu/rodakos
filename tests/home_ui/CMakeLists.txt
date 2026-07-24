cmake_minimum_required(VERSION 3.20)

project(rodakos_home_ui_tests LANGUAGES C CXX)

enable_testing()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LVGL_ROOT "${RODAKOS_ROOT}/managed_components/lvgl__lvgl")

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")

add_executable(rodakos_home_ui_tests
    test_main.cc
    home_ui_test.cc
    fakes/test_fonts.cc
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
    "${RODAKOS_ROOT}/tests/app_model/third_party/cjson/cJSON.c"
)

target_include_directories(rodakos_home_ui_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RODAKOS_ROOT}/main"
    "${RODAKOS_ROOT}/tests/app_model/third_party/cjson"
    "${RODAKOS_ROOT}/managed_components/78__xiaozhi-fonts/include"
)

target_link_libraries(rodakos_home_ui_tests PRIVATE lvgl)

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(rodakos_home_ui_tests PRIVATE m Threads::Threads)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_home_ui_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(NAME rodakos_home_ui COMMAND rodakos_home_ui_tests)
