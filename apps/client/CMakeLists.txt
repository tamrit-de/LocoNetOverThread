cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/lnot_firmware_version.cmake")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(loconet_thread_client)
