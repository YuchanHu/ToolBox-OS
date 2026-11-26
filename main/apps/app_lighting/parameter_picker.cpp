#include "parameter_picker.h"
#include <system/ui.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr int ruler_width = 310;
constexpr int ruler_height = 89;

void rect(lv_layer_t *layer, int x, int y, int w, int h, uint32_t color, lv_opa_t opacity = LV_OPA_COVER) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = opacity;
    lv_area_t area{x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &dsc, &area);
}

void caption(lv_layer_t *layer, const char *text, int x, int y, int half_width, const lv_font_t *font,
             uint32_t color, lv_opa_t opacity = LV_OPA_COVER) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.text_local = true;
    dsc.font = font;
    dsc.color = lv_color_hex(color);
    dsc.opa = opacity;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    const int top = y - font->line_height / 2;
    const lv_area_t area{x - half_width, top, x + half_width, top + font->line_height - 1};
    lv_draw_label(layer, &dsc, &area);
}
} // namespace

void LightingParameterPicker::create(lv_obj_t *parent, int x, int y, Scale scale, Commit commit,
                                     void *context) {
    scale_ = scale;
    first_grid_ = (scale.minimum / scale.step + 1) * scale.step;
    last_index_ = (scale.maximum - 1) / scale.step - scale.minimum / scale.step + 1;
    area_ = toolbox::ui::panel(parent, x, y, ruler_width, ruler_height);
    lv_obj_set_style_bg_opa(area_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(area_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(area_, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(area_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(area_, event, LV_EVENT_ALL, this);
    commit_ = commit;
    context_ = context;
    pressed_ = false;
    selected_index_ = -1;
    offset_ = -1;
    toolbox::ui::text(area_, scale.unit == Unit::Kelvin ? "色温" : "亮度", 9, 11, 68,
                      &toolbox_misans_lighting_20, 0xa8b5c5);
    number_ = toolbox::ui::text(area_, "", 0, 0, ruler_width, &toolbox_misans_36);
}

void LightingParameterPicker::create_brightness(lv_obj_t *parent, int x, int y, int value, bool decimal,
                                                Commit commit, void *context) {
    // Draw one tick per percent, while decimal models allow one tenth per pixel.
    const int step = decimal ? 1 : 10;
    create(parent, x, y, {decimal ? Unit::DecimalPercent : Unit::Percent, 0, 1000, step, step, 10 / step, 50},
           commit, context);
    set_value(value);
}

void LightingParameterPicker::create_temperature(lv_obj_t *parent, int x, int y, int value, int minimum,
                                                 int maximum, int step, Commit commit, void *context) {
    create(parent, x, y, {Unit::Kelvin, minimum, maximum, step, 12, 1, 5 * step}, commit, context);
    set_value(value);
}

int LightingParameterPicker::value_at(int index) const {
    if (index <= 0)
        return scale_.minimum;
    if (index >= last_index_)
        return scale_.maximum;
    return first_grid_ + (index - 1) * scale_.step;
}

int LightingParameterPicker::nearest_index(int value) const {
    const int step = scale_.step;
    const int next = std::clamp((value - first_grid_ + step - 1) / step + 1, 1, last_index_);
    return value - value_at(next - 1) < value_at(next) - value ? next - 1 : next;
}

int LightingParameterPicker::snapped_index() const {
    return (offset_ + scale_.pixels_per_step / 2) / scale_.pixels_per_step;
}

void LightingParameterPicker::move_to(int offset) {
    if (offset == offset_)
        return;
    offset_ = offset;
    const int value = value_at(snapped_index());
    switch (scale_.unit) {
    case Unit::Kelvin:
        toolbox::ui::set_text_fmt(number_, "%d K", value);
        break;
    case Unit::Percent:
        toolbox::ui::set_text_fmt(number_, "%d%%", value / 10);
        break;
    case Unit::DecimalPercent:
        toolbox::ui::set_text_fmt(number_, "%d.%d%%", value / 10, value % 10);
        break;
    }
    lv_area_t ruler;
    lv_obj_get_coords(area_, &ruler);
    ruler.y1 += 40;
    lv_obj_invalidate_area(area_, &ruler);
}

void LightingParameterPicker::set_value(int value) {
    if (!area_ || pressed_)
        return;
    selected_index_ = nearest_index(value);
    move_to(selected_index_ * scale_.pixels_per_step);
}

void LightingParameterPicker::draw(lv_event_t *event) const {
    lv_area_t area;
    lv_obj_get_coords(area_, &area);
    auto *layer = lv_event_get_layer(event);
    const int center_x = (area.x1 + area.x2 + 1) / 2;
    const int base = area.y1 + 63;
    const int pixels = scale_.pixels_per_step;
    const int tick_steps = scale_.steps_per_tick;
    const int first = std::max(0, (offset_ - ruler_width / 2) / (pixels * tick_steps)) * tick_steps;
    const int last = std::min(last_index_, (offset_ + ruler_width / 2) / pixels);
    const bool kelvin = scale_.unit == Unit::Kelvin;
    const int label_margin = kelvin ? 27 : 14;
    for (int index = first; index <= last; index += tick_steps) {
        const int x = center_x + index * pixels - offset_;
        const int value = value_at(index);
        const bool labeled = value % scale_.major_interval == 0;
        const bool major = labeled || index == 0 || index == last_index_;
        const auto opacity = static_cast<lv_opa_t>(
            std::clamp((ruler_width / 2 - std::abs(x - center_x)) * 7, 0, 255));
        rect(layer, x, base - (major ? 17 : 8), major ? 2 : 1, major ? 17 : 8, 0x8a9bb2, opacity);
        if (labeled && x >= area.x1 + label_margin && x <= area.x2 - label_margin) {
            char label[16];
            std::snprintf(label, sizeof(label), "%d", kelvin ? value : value / 10);
            caption(layer, label, x, base + 12, label_margin, &toolbox_misans_18, 0xa8b5c5, opacity);
        }
    }
    rect(layer, center_x, base - 23, 2, 25, toolbox::ui::accent);
}

void LightingParameterPicker::touch(lv_event_code_t code) {
    if (code == LV_EVENT_PRESS_LOST) {
        if (pressed_) {
            pressed_ = false;
            move_to(selected_index_ * scale_.pixels_per_step);
        }
        return;
    }
    auto *input = lv_indev_active();
    if (!input)
        return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    const int position = point.x;
    const uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        pressed_ = true;
        previous_point_ = position;
        previous_tick_ = now;
        return;
    }
    if (!pressed_)
        return;
    const int delta = previous_point_ - position;
    if (delta) {
        const auto elapsed = std::max<uint32_t>(12, now - previous_tick_);
        const int speed = std::abs(delta) * 16 / static_cast<int>(elapsed);
        const int gain = 1 + std::clamp((speed - 7) / 6, 0, 4);
        const int limit = last_index_ * scale_.pixels_per_step;
        move_to(std::clamp(offset_ + delta * gain, 0, limit));
        previous_point_ = position;
        previous_tick_ = now;
    }
    if (code != LV_EVENT_RELEASED)
        return;

    pressed_ = false;
    const int index = snapped_index();
    const bool changed = index != selected_index_;
    selected_index_ = index;
    move_to(index * scale_.pixels_per_step);
    if (changed)
        commit_(context_, value_at(index));
}

void LightingParameterPicker::event(lv_event_t *event) {
    auto &picker = *static_cast<LightingParameterPicker *>(lv_event_get_user_data(event));
    switch (lv_event_get_code(event)) {
    case LV_EVENT_DRAW_MAIN:
        picker.draw(event);
        break;
    case LV_EVENT_PRESSED:
    case LV_EVENT_PRESSING:
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        picker.touch(lv_event_get_code(event));
        break;
    case LV_EVENT_DELETE:
        picker.area_ = picker.number_ = nullptr;
        break;
    default:
        break;
    }
}
