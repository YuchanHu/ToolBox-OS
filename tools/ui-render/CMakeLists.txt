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
set(SONY_MEDIA "${ROOT}/main/sony/media.cpp" "${ROOT}/main/sony/media_image.cpp"
    "${ROOT}/components/M5GFX/src/lgfx/utility/lgfx_tjpgd.c")
add_executable(render render.cpp extensions.cpp lighting_preview.cpp ${FONTS}
    sony_mock.cpp
    "${ROOT}/main/sony/connection_ui.cpp" "${ROOT}/main/sony/keypad.cpp" "${ROOT}/main/sony/credentials.cpp"
    "${ROOT}/main/sony/capture_ui.cpp" "${ROOT}/main/sony/capture.cpp" "${ROOT}/main/sony/session.cpp"
    "${ROOT}/main/sony/body.cpp" "${ROOT}/main/sony/body_ui.cpp"
    "${ROOT}/main/sony/automation.cpp" "${ROOT}/main/sony/automation_ui.cpp"
    "${ROOT}/main/sony/media_ui.cpp" ${SONY_MEDIA}
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
    "${ROOT}/main/apps/app_lighting/app_lighting.cpp" "${ROOT}/main/apps/app_lighting/parameter_picker.cpp"
    "${ROOT}/main/system/lighting/products.cpp" "${ROOT}/main/assets/lighting_icon.c"
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
target_include_directories(render PRIVATE "${ROOT}/components/mooncake_log/src" "${ROOT}/components/M5GFX/src")
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
add_executable(lighting_protocol_check "${ROOT}/tools/lighting-test/test.cpp"
    "${ROOT}/main/system/lighting/protocol.cpp" "${ROOT}/main/system/lighting/products.cpp")
target_include_directories(lighting_protocol_check PRIVATE "${ROOT}/main/system/lighting")
add_executable(sony_protocol_check "${ROOT}/tools/sony-test/test.cpp"
    "${ROOT}/main/sony/automation.cpp"
    "${ROOT}/main/sony/session.cpp" "${ROOT}/main/sony/capture.cpp" "${ROOT}/main/sony/body.cpp" ${SONY_MEDIA})
target_include_directories(sony_protocol_check PRIVATE "${ROOT}/main" "${ROOT}/components/M5GFX/src")
add_executable(sony_connection_check "${ROOT}/tools/sony-test/connection_check.cpp"
    "${ROOT}/main/sony/keypad.cpp" "${ROOT}/main/sony/credentials.cpp")
target_include_directories(sony_connection_check PRIVATE "${ROOT}/main")
if(MSVC)
    target_compile_options(camera_backend_check PRIVATE /utf-8 /EHsc)
    target_compile_options(lighting_protocol_check PRIVATE /utf-8 /EHsc)
    target_compile_options(sony_protocol_check PRIVATE /utf-8 /EHsc)
    target_compile_options(sony_connection_check PRIVATE /utf-8 /EHsc)
endif()

option(SONY_SSH_LOOPBACK_TEST "Build the Windows SSH interop test using the selected IDF's mbedTLS source" OFF)
if(MSVC AND SONY_SSH_LOOPBACK_TEST)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    add_subdirectory("$ENV{IDF_PATH}/components/mbedtls/mbedtls" sony-test-mbedtls EXCLUDE_FROM_ALL)
    add_subdirectory("${ROOT}/components/camera_ssh" sony-test-libssh2 EXCLUDE_FROM_ALL)
    add_executable(sony_ssh_check "${ROOT}/tools/sony-test/ssh_loopback.cpp"
        "${ROOT}/main/sony/tunnel.cpp" "${ROOT}/main/sony/session.cpp")
    target_include_directories(sony_ssh_check PRIVATE "${ROOT}/main")
    target_compile_definitions(sony_ssh_check PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(sony_ssh_check PRIVATE /utf-8 /EHsc)
    target_link_libraries(sony_ssh_check PRIVATE camera_ssh_host mbedcrypto ws2_32)
endif()

