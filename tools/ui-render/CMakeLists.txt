cmake_minimum_required(VERSION 3.20)
project(ToolBoxUiRender LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/components/lvgl" lvgl)
file(GLOB FONTS "${ROOT}/main/assets/fonts/toolbox_misans_*.c")
add_executable(render render.cpp extensions.cpp ${FONTS}
    "${ROOT}/main/assets/app_icons.c"
    "${ROOT}/main/assets/images/icon_indicator_left.c"
    "${ROOT}/main/assets/images/icon_indicator_right.c"
    "${ROOT}/main/system/weather_decode.cpp"
    "F:/esp/.espressif/v5.5.5/esp-idf/components/json/cJSON/cJSON.c"
    "${ROOT}/main/system/extension_ui.cpp"
    "${ROOT}/main/apps/app_extensions/app_extensions.cpp"
    "${ROOT}/main/system/touch_input.cpp" "${ROOT}/main/system/ui.cpp" "${ROOT}/main/system/shell.cpp"
    "${ROOT}/main/system/stopwatch_ui.cpp" "${ROOT}/main/system/camera_ui.cpp"
    "${ROOT}/main/apps/app_camera/app_camera.cpp" "${ROOT}/main/assets/ui_icons.c"
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
target_include_directories(render PRIVATE "F:/esp/.espressif/v5.5.5/esp-idf/components/json/cJSON")
target_link_libraries(render PRIVATE lvgl::lvgl)
target_compile_options(render PRIVATE /utf-8)
target_compile_definitions(render PRIVATE _CRT_SECURE_NO_WARNINGS FMT_HEADER_ONLY=1)

add_executable(camera_backend_check "${ROOT}/tools/camera-test/test.cpp"
    "${ROOT}/main/system/camera_remote.cpp")
target_include_directories(camera_backend_check BEFORE PRIVATE "${ROOT}/tools/camera-test" "${ROOT}/main")
target_compile_options(camera_backend_check PRIVATE /utf-8 /EHsc)

