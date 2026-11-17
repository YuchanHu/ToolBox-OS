#pragma once
#include "fonts.h"
#include "icons.h"

namespace toolbox::ui {
constexpr uint32_t accent = 0x9dc6ff;
constexpr int content_top = 144;
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color = 0, int radius = 0);
lv_obj_t* text(lv_obj_t* parent, const char* value, int x, int y, int width,
               const lv_font_t* font = &toolbox_misans_28, uint32_t color = 0xf3f6fc);
lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* source, int x, int y, uint32_t color = 0xf3f6fc);
lv_obj_t* button(lv_obj_t* parent, const char* value, int x, int y, int width, int height,
                 lv_event_cb_t callback, uint32_t color = 0x202631, const lv_font_t* font = &toolbox_misans_23);
lv_obj_t* icon_button(lv_obj_t* parent, const lv_image_dsc_t* source, int x, int y, int width,
                      int height, lv_event_cb_t callback, uint32_t color = 0x202631);
void heading(lv_obj_t* parent, const char* value, lv_event_cb_t back);
lv_obj_t* boot(lv_obj_t* parent);
lv_obj_t* row(lv_obj_t* parent, int x, int y, int width, int height, int gap = 12);
lv_obj_t* row_text(lv_obj_t* parent, const char* value, const lv_font_t* font, uint32_t color = 0xf3f6fc);
lv_obj_t* battery(lv_obj_t* parent, uint32_t color = 0xf3f6fc);
void battery_level(lv_obj_t* object, int percent, uint32_t color = 0xf3f6fc);
lv_obj_t* charging_icon(lv_obj_t* parent, uint32_t color = 0xf3f6fc);
}
