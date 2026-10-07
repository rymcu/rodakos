cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.2-dev.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rodakos)

include(cmake/codec_volume_patch.cmake)
include(cmake/mqtt_event_patch.cmake)
include(cmake/websocket_redirect_patch.cmake)
include(cmake/lodepng_patch.cmake)
include(cmake/screen_jpeg_allocator.cmake)
idf_component_get_property(rodak_lvgl_dir lvgl__lvgl COMPONENT_DIR)
idf_component_get_property(rodak_lvgl_target lvgl__lvgl COMPONENT_LIB)
idf_build_get_property(rodak_patch_python PYTHON)
rodak_apply_lodepng_patch(${rodak_lvgl_target} "${rodak_lvgl_dir}" "${PROJECT_DIR}" "${rodak_patch_python}")
