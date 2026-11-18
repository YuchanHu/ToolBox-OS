#include "ui.h"
#include <cstring>

namespace toolbox::ui {
bool set_text(lv_obj_t* label, const char* value)
{
    if (std::strcmp(lv_label_get_text(label), value) == 0) return false;
    lv_label_set_text(label, value);
    return true;
}
namespace {
void center_cap_height(lv_obj_t* label, const lv_font_t* font)
{
    lv_font_glyph_dsc_t glyph = {};
    if (lv_font_get_glyph_dsc(font, &glyph, '0', 0)) {
        const int center = font->line_height - font->base_line - glyph.ofs_y - glyph.box_h / 2;
        lv_obj_set_style_translate_y(label, font->line_height / 2 - center, 0);
    }
}
}
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color, int radius)
{
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_remove_flag(object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return object;
}

lv_obj_t* text(lv_obj_t* parent, const char* value, int x, int y, int width,
               const lv_font_t* font, uint32_t color)
{
    auto* object = lv_label_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(object, width);
    lv_obj_set_pos(object, x, y);
    lv_label_set_text(object, value);
    return object;
}

lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* source, int x, int y, uint32_t color)
{
    auto* object = lv_image_create(parent);
    lv_image_set_src(object, source);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_style_image_recolor(object, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(object, LV_OPA_COVER, 0);
    return object;
}

lv_obj_t* button(lv_obj_t* parent, const char* value, int x, int y, int width, int height,
                 lv_event_cb_t callback, uint32_t color, const lv_font_t* font)
{
    auto* object = panel(parent, x, y, width, height, color, 24);
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    auto* caption = text(object, value, 0, 0, width, font, color == accent ? 0x081627 : 0xf3f6fc);
    lv_obj_center(caption);
    center_cap_height(caption, font);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x344861), LV_STATE_PRESSED);
    return object;
}

lv_obj_t* icon_button(lv_obj_t* parent, const lv_image_dsc_t* source, int x, int y, int width,
                      int height, lv_event_cb_t callback, uint32_t color)
{
    auto* object = button(parent, "", x, y, width, height, callback, color);
    icon(object, source, (width - source->header.w) / 2, (height - source->header.h) / 2,
         color == accent ? 0x142b47 : 0xa8caff);
    return object;
}

void heading(lv_obj_t* parent, const char* value, lv_event_cb_t back)
{
    text(parent, value, 110, 47, 246, &toolbox_misans_26);
    icon_button(parent, &toolbox_icon_back_28, 63, 82, 46, 46, back);
}

lv_obj_t* boot(lv_obj_t* parent)
{
    auto* root = panel(parent, 0, 0, 466, 466);
    auto* mark = icon(root, &toolbox_icon_box_78, 194, 144, 0xa8caff);
    lv_image_set_scale(mark, 236);
    text(root, "ToolBox OS", 0, 245, 466, &toolbox_misans_32);
    return root;
}

lv_obj_t* row(lv_obj_t* parent, int x, int y, int width, int height, int gap)
{
    auto* object = panel(parent, x, y, width, height);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(object, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(object, gap, 0);
    return object;
}

lv_obj_t* row_text(lv_obj_t* parent, const char* value, const lv_font_t* font, uint32_t color)
{
    auto* object = text(parent, value, 0, 0, LV_SIZE_CONTENT, font, color);
    center_cap_height(object, font);
    return object;
}

lv_obj_t* battery(lv_obj_t* parent, uint32_t color)
{
    auto* object = panel(parent, 0, 0, 28, 28);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    auto* outline = panel(object, 2, 7, 20, 14, 0, 2);
    lv_obj_set_style_bg_opa(outline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(outline, 2, 0);
    panel(object, 23, 11, 2, 6, color, 1);
    panel(object, 5, 10, 14, 8, color, 1);
    battery_level(object, 100, color);
    return object;
}

lv_obj_t* charging_icon(lv_obj_t* parent, uint32_t color)
{
    auto* object = panel(parent, 0, 0, 14, 28);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    static const lv_point_precise_t points[] = {{10, 4}, {3, 15}, {11, 13}, {4, 24}};
    auto* bolt = lv_line_create(object);
    lv_line_set_points(bolt, points, 4);
    lv_obj_set_style_line_width(bolt, 2, 0);
    lv_obj_set_style_line_color(bolt, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(bolt, true, 0);
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    return object;
}

void battery_level(lv_obj_t* object, int percent, uint32_t color)
{
    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    auto* body = lv_obj_get_child(object, 0);
    auto* cap = lv_obj_get_child(object, 1);
    auto* fill = lv_obj_get_child(object, 2);
    const auto shade = lv_color_hex(color);
    if (!lv_color_eq(lv_obj_get_style_border_color(body, LV_PART_MAIN), shade))
        lv_obj_set_style_border_color(body, shade, 0);
    if (!lv_color_eq(lv_obj_get_style_bg_color(cap, LV_PART_MAIN), shade))
        lv_obj_set_style_bg_color(cap, shade, 0);
    if (lv_obj_get_style_width(fill, LV_PART_MAIN) != 14 * percent / 100)
        lv_obj_set_width(fill, 14 * percent / 100);
    lv_obj_set_flag(fill, LV_OBJ_FLAG_HIDDEN, percent == 0);
    if (!lv_color_eq(lv_obj_get_style_bg_color(fill, LV_PART_MAIN), shade))
        lv_obj_set_style_bg_color(fill, shade, 0);
}
}
