cmake_minimum_required(VERSION 3.20)
project(rodakos_remote_input_tests LANGUAGES C CXX)
enable_testing()
get_filename_component(RODAKOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${RODAKOS_ROOT}/tests/home_ui/lv_conf.h" CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${RODAKOS_ROOT}/managed_components/lvgl__lvgl" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
add_executable(rodakos_remote_input_tests
    remote_input_test.cc
    "${RODAKOS_ROOT}/main/phone_os/remote_input_controller.cc"
    "${RODAKOS_ROOT}/managed_components/espressif__cjson/cJSON/cJSON.c"
)
target_compile_features(rodakos_remote_input_tests PRIVATE cxx_std_20)
target_include_directories(rodakos_remote_input_tests PRIVATE
    "${RODAKOS_ROOT}/main" "${RODAKOS_ROOT}/tests/app_model"
    "${RODAKOS_ROOT}/managed_components/espressif__cjson/cJSON")
target_link_libraries(rodakos_remote_input_tests PRIVATE lvgl)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(rodakos_remote_input_tests PRIVATE m Threads::Threads)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rodakos_remote_input_tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME rodakos_remote_input COMMAND rodakos_remote_input_tests)
set_tests_properties(rodakos_remote_input PROPERTIES TIMEOUT 40)
