if(NOT CMAKE_SCRIPT_MODE_FILE)
    set(track_source_changes CONFIGURE_DEPENDS)
endif()
file(GLOB_RECURSE MY_SRCS ${track_source_changes}
    "apps/app_camera/*.cpp"
    "apps/app_lighting/*.cpp"
    "apps/app_counter/*.cpp"
    "apps/app_timers/*.cpp"
    "apps/app_focus/*.cpp"
    "apps/app_level/*.cpp"
    "apps/app_weather/*.cpp"
    "apps/common/countdown/*.cpp"
    "apps/common/ui/*.cpp"
    "apps/app_launcher/*.cpp"
    "apps/app_stopwatch/model/*.cpp"
    "apps/common/audio/*.cpp"
    "apps/common/key_manager/*.cpp"
    "assets/fonts/toolbox_misans_*.c"
    "hal/*.c"
    "hal/*.cc"
    "hal/*.cpp"
    "system/*.cpp"
)

list(APPEND MY_SRCS
    "apps/app_setup/app_setup.cpp"
    "apps/app_stopwatch/app_stopwatch.cpp"
    "assets/ui_icons.c"
    "assets/camera_remote_icons.c"
    "assets/boot_logo.c"
    "assets/app_icons.c"
    "assets/lighting_icon.c"
    "assets/images/icon_indicator_left.c"
    "assets/images/icon_indicator_right.c"
    "assets/images/icon_badge.c"
)

set(MY_INCS
    "."
)

idf_component_register(
    SRCS 
        "main.cpp"
        ${MY_SRCS}
    INCLUDE_DIRS 
        ${MY_INCS}
    EMBED_FILES
        "assets/sfx/boot_sfx.bin"
    EMBED_TXTFILES
        "hal/utils/config_ap/assets/badge_config_ap.html"
)
