file(GLOB_RECURSE MY_SRCS
    "apps/app_camera/*.cpp"
    "apps/app_extensions/*.cpp"
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
    "assets/app_icons.c"
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
