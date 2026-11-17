cmake_minimum_required(VERSION 3.16)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/main/system/fonts.h")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/main/assets/provision.html"
               "${CMAKE_CURRENT_SOURCE_DIR}/components/esp-wifi-connect/assets/wifi_configuration.html"
               COPYONLY)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ToolBoxOS VERSION 1.0.0)
