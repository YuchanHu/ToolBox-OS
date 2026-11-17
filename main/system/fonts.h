#pragma once
#include <lvgl.h>
#ifdef __cplusplus
extern "C" {
#endif
LV_FONT_DECLARE(toolbox_misans_20);
LV_FONT_DECLARE(toolbox_misans_18);
LV_FONT_DECLARE(toolbox_misans_22);
LV_FONT_DECLARE(toolbox_misans_24);
LV_FONT_DECLARE(toolbox_misans_semibold_36);
LV_FONT_DECLARE(toolbox_misans_semibold_65);
LV_FONT_DECLARE(toolbox_misans_28);
LV_FONT_DECLARE(toolbox_misans_32);
LV_FONT_DECLARE(toolbox_misans_23);
LV_FONT_DECLARE(toolbox_misans_25);
LV_FONT_DECLARE(toolbox_misans_26);
LV_FONT_DECLARE(toolbox_misans_65);
LV_FONT_DECLARE(toolbox_misans_98);
LV_FONT_DECLARE(toolbox_misans_36);
LV_FONT_DECLARE(toolbox_misans_64);
LV_FONT_DECLARE(toolbox_misans_96);
#ifdef __cplusplus
}
#endif
namespace toolbox::fonts {
inline const lv_font_t* body() { return &toolbox_misans_28; }
}
