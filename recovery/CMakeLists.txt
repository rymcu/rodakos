cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3" CACHE STRING "" FORCE)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components/rodak_ota_state")
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
set(COMPONENTS main)
set(RODAK_OTA_FAULT_INJECTION_PHASE "" CACHE STRING
    "Recovery reset-injection phase for journal recovery tests")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rodakos_recovery)
