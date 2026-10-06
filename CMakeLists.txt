cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.2-dev.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rodakos)

include(cmake/codec_volume_patch.cmake)
include(cmake/mqtt_event_patch.cmake)
include(cmake/websocket_redirect_patch.cmake)
