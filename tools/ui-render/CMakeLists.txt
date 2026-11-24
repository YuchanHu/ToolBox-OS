cmake_minimum_required(VERSION 3.20)
project(ToolBoxUiRender LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(CJSON_SOURCE_DIR "$ENV{IDF_PATH}/components/json/cJSON" CACHE PATH "Directory containing cJSON.c and cJSON.h")
if(NOT EXISTS "${CJSON_SOURCE_DIR}/cJSON.c" OR NOT EXISTS "${CJSON_SOURCE_DIR}/cJSON.h")
    message(FATAL_ERROR "cJSON is missing. Export IDF_PATH or pass -DCJSON_SOURCE_DIR=<cJSON source directory>.")
endif()
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/components/lvgl" lvgl)
file(GLOB FONTS CONFIGURE_DEPENDS "${ROOT}/main/assets/fonts/toolbox_misans_*.c")
add_executable(render render.cpp extensions.cpp ${FONTS}
    "${ROOT}/main/assets/app_icons.c"
    "${ROOT}/main/assets/boot_logo.c"
    "${ROOT}/main/assets/images/icon_indicator_left.c"
    "${ROOT}/main/assets/images/icon_indicator_right.c"
    "${ROOT}/main/system/weather_decode.cpp"
    "${CJSON_SOURCE_DIR}/cJSON.c"
    "${ROOT}/main/system/extension_ui.cpp"
    "${ROOT}/main/apps/app_counter/app_counter.cpp"
    "${ROOT}/main/apps/app_timers/app_timers.cpp"
    "${ROOT}/main/apps/app_focus/app_focus.cpp"
    "${ROOT}/main/apps/app_level/app_level.cpp"
    "${ROOT}/main/apps/app_weather/app_weather.cpp"
    "${ROOT}/main/apps/common/countdown/countdown_alert.cpp"
    "${ROOT}/main/apps/common/ui/tool_app.cpp"
    "${ROOT}/main/system/touch_input.cpp" "${ROOT}/main/system/ui.cpp" "${ROOT}/main/system/shell.cpp"
    "${ROOT}/main/system/stopwatch_ui.cpp" "${ROOT}/main/system/camera_ui.cpp"
    "${ROOT}/main/apps/app_camera/app_camera.cpp" "${ROOT}/main/assets/ui_icons.c"
    "${ROOT}/main/assets/camera_remote_icons.c"
    "${ROOT}/main/apps/app_launcher/view/view.cpp"
    "${ROOT}/main/apps/app_stopwatch/app_stopwatch.cpp"
    "${ROOT}/main/apps/app_stopwatch/model/stopwatch.cpp"
    "${ROOT}/main/apps/common/key_manager/key_manager.cpp"
    "${ROOT}/main/hal/utils/button/Button_Class.cpp"
    "${ROOT}/components/mooncake/src/ability/app_ability.cpp")
target_include_directories(render BEFORE PRIVATE stubs "${ROOT}/main"
    "${ROOT}/components/mooncake/src" "${ROOT}/components/smooth_ui_toolkit/src"
    "${ROOT}/components/smooth_ui_toolkit/src/lvgl")
target_include_directories(render PRIVATE "${ROOT}/components/mooncake_log/src")
target_include_directories(render PRIVATE "${CJSON_SOURCE_DIR}")
target_link_libraries(render PRIVATE lvgl::lvgl)
if(MSVC)
    target_compile_options(render PRIVATE /utf-8 /EHsc)
endif()
target_compile_definitions(render PRIVATE _CRT_SECURE_NO_WARNINGS FMT_HEADER_ONLY=1)
target_compile_definitions(render PRIVATE TOOLBOX_UI_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}")

add_executable(camera_backend_check "${ROOT}/tools/camera-test/test.cpp"
    "${ROOT}/main/system/camera_remote.cpp")
target_include_directories(camera_backend_check BEFORE PRIVATE "${ROOT}/tools/camera-test" "${ROOT}/main")
if(MSVC)
    target_compile_options(camera_backend_check PRIVATE /utf-8 /EHsc)
endif()

