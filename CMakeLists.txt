cmake_minimum_required(VERSION 3.16)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/main/system/fonts.h")
if(NOT DEFINED ENV{IDF_PATH} OR NOT EXISTS "$ENV{IDF_PATH}/tools/cmake/project.cmake")
    message(FATAL_ERROR "ESP-IDF not found. Export the ESP-IDF 5.5.5 environment before configuring.")
endif()
include("$ENV{IDF_PATH}/tools/cmake/project.cmake")
project(ToolBoxOS VERSION 1.2.0)
