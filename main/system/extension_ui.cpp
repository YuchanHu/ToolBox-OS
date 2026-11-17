#include "extension_ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <src/misc/cache/instance/lv_image_cache.h>

namespace toolbox {
namespace {
void label(lv_obj_t *object, const std::string &text) {
    if (object && text != lv_label_get_text(object))
        lv_label_set_text(object, text.c_str());
}
const char *title(Extension kind) {
    switch (kind) {
    case Extension::Counter:
        return "计数器";
    case Extension::Timers:
        return "倒计时";
    case Extension::Focus:
        return "番茄钟";
    case Extension::Level:
        return "水平仪设置";
    case Extension::Weather:
        return "天气";
    }
    return "";
}
std::string number(double value) {
    char text[32];
    snprintf(text, sizeof(text), "%05.2f", value);
    return text;
}
} // namespace

ExtensionView::ExtensionView(Extension kind) : kind_(kind) {
    root_ = ui::panel(lv_screen_active(), 1, 0, 466, 466);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root_, event, LV_EVENT_CLICKED, this);
}
ExtensionView::~ExtensionView() {
    lv_obj_delete(root_);
}
ExtensionEvent ExtensionView::take_event() {
    auto event = pending_;
    pending_ = {};
    return event;
}

lv_obj_t *ExtensionView::button(lv_obj_t *parent, const char *caption, int x, int y, int width, int height,
                                ExtensionAction action, int value, bool accent) {
    auto *object =
        ui::button(parent, caption, x, y, width, height, [](lv_event_t *) {}, accent ? ui::accent : 0x202631);
    lv_obj_set_user_data(object, reinterpret_cast<void *>((static_cast<uintptr_t>(action) << 16) | (value & 0xffff)));
    lv_obj_add_event_cb(object, event, LV_EVENT_CLICKED, this);
    return object;
}
void ExtensionView::event(lv_event_t *event) {
    auto *self = static_cast<ExtensionView *>(lv_event_get_user_data(event));
    auto *object = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (object == self->root_) {
        self->reveal(lv_tick_get());
        return;
    }
    if (self->pending_.action != ExtensionAction::None)
        return;
    const uintptr_t code = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(object));
    self->pending_.action = static_cast<ExtensionAction>(code >> 16);
    self->pending_.value = static_cast<int16_t>(code & 0xffff);
    if (self->input_)
        self->pending_.text = lv_textarea_get_text(self->input_);
    self->reveal(lv_tick_get());
}
void ExtensionView::reveal(uint32_t now) {
    if (!controls_)
        return;
    revealed_at_ = now;
    lv_obj_remove_flag(controls_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(controls_, 255, 0);
}

void ExtensionView::build() {
    lv_obj_clean(root_);
    content_ = controls_ = dialog_ = time_ = detail_ = message_ = ring_ = drawing_ = primary_ = input_ = nullptr;
    keyboard_ = results_ = nullptr;
    values_.clear();
    built_confirm_ = false;
    city_revision_ = UINT32_MAX;
    content_ = ui::panel(root_, 0, 0, 466, 466);
    const bool main = state_.page == ExtensionPage::Main;
    if (main && kind_ == Extension::Weather) {
        drawing_ = ui::panel(content_, 0, 0, 466, 466);
        lv_obj_add_event_cb(drawing_, draw, LV_EVENT_DRAW_MAIN, this);
        message_ = ui::text(content_, "", 0, 62, 466, &toolbox_misans_26);
        detail_ = ui::text(content_, "", 0, 125, 466, &toolbox_misans_20);
        time_ = ui::text(content_, "--°", 0, 170, 466, &toolbox_misans_65);
        values_.push_back(ui::text(content_, "", 0, 261, 466));
        values_.push_back(ui::text(content_, "", 0, 316, 466, &toolbox_misans_20));
        values_.push_back(ui::text(content_, "", 63, 354, 340, &toolbox_misans_20));
        values_.push_back(ui::text(content_, "", 83, 389, 300, &toolbox_misans_20));
        controls_ = ui::panel(content_, 0, 0, 466, 466);
        lv_obj_set_style_bg_opa(controls_, LV_OPA_TRANSP, 0);
        auto *back = button(controls_, "", 63, 82, 46, 46, ExtensionAction::Back);
        ui::icon(back, &toolbox_icon_back_28, 9, 9);
        lv_obj_add_flag(controls_, LV_OBJ_FLAG_HIDDEN);
        button(controls_, "详情", 94, 355, 130, 44, ExtensionAction::Edit);
        button(controls_, "刷新", 240, 355, 130, 44, ExtensionAction::Refresh);
        return;
    }
    if (main && kind_ == Extension::Level) {
        drawing_ = ui::panel(content_, 0, 0, 466, 466);
        lv_obj_add_event_cb(drawing_, draw, LV_EVENT_DRAW_MAIN, this);
        message_ = ui::text(content_, "PINPOINT", 0, 108, 466, &toolbox_misans_23);
        time_ = ui::text(content_, "--.--°", 0, 203, 466, &toolbox_misans_semibold_65);
        detail_ = ui::text(content_, "", 0, 290, 466, &toolbox_misans_20);
    } else {
        const char *page_title = title(kind_);
        if (state_.page == ExtensionPage::City)
            page_title = "选择城市";
        if (state_.page == ExtensionPage::Details)
            page_title = "天气详情";
        if (state_.page == ExtensionPage::Custom)
            page_title = "自定义时间";
        if (state_.page == ExtensionPage::Calibration)
            page_title = "翻转校准";
        if (state_.page == ExtensionPage::Settings && kind_ == Extension::Focus)
            page_title = "专注设置";
        ui::heading(content_, page_title, [](lv_event_t *) {});
        auto *back = lv_obj_get_child(content_, 1);
        lv_obj_set_user_data(back, reinterpret_cast<void *>(static_cast<uintptr_t>(ExtensionAction::Back) << 16));
        lv_obj_add_event_cb(back, event, LV_EVENT_CLICKED, this);
        if (main)
            lv_obj_add_flag(back, LV_OBJ_FLAG_HIDDEN);
    }
    if (main) {
        if (kind_ == Extension::Counter) {
            time_ = ui::text(content_, "0", 0, 160, 466, &toolbox_misans_98);
            lv_obj_align(time_, LV_ALIGN_CENTER, 0, 0);
        }
        if (kind_ != Extension::Level && kind_ != Extension::Counter) {
            ring_ = lv_arc_create(content_);
            lv_obj_remove_style_all(ring_);
            lv_obj_set_size(ring_, 460, 460);
            lv_obj_set_pos(ring_, 3, 3);
            lv_arc_set_bg_angles(ring_, 0, 360);
            lv_arc_set_rotation(ring_, 270);
            lv_arc_set_range(ring_, 0, 1000);
            lv_obj_set_style_arc_color(ring_, lv_color_hex(0x202631), LV_PART_MAIN);
            lv_obj_set_style_arc_color(ring_, lv_color_hex(ui::accent), LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(ring_, 5, LV_PART_MAIN);
            lv_obj_set_style_arc_width(ring_, 5, LV_PART_INDICATOR);
            lv_obj_remove_flag(ring_, LV_OBJ_FLAG_CLICKABLE);
            time_ = ui::text(content_, "", 0, 202, 466, &toolbox_misans_65);
            detail_ = ui::text(content_, "", 0, 277, 466, &toolbox_misans_20, ui::accent);
        }
        controls_ = ui::panel(content_, 0, 0, 466, 466);
        lv_obj_set_style_bg_opa(controls_, LV_OPA_TRANSP, 0);
        auto *back = button(controls_, "", 63, 82, 46, 46, ExtensionAction::Back);
        ui::icon(back, &toolbox_icon_back_28, 9, 9, ui::accent);
        if (kind_ == Extension::Level) {
            auto *back = button(controls_, "", 106, 361, 58, 46, ExtensionAction::Back);
            ui::icon(back, &toolbox_icon_back_28, 15, 9, ui::accent);
            button(controls_, "设置", 180, 361, 106, 46, ExtensionAction::Settings);
            button(controls_, "声音", 296, 361, 70, 46, ExtensionAction::Sound);
            lv_obj_add_flag(lv_obj_get_child(controls_, 0), LV_OBJ_FLAG_HIDDEN);
        } else if (kind_ == Extension::Focus) {
            primary_ = button(controls_, "开始", 143, 350, 180, 48, ExtensionAction::Primary, 0, true);
            button(controls_, "设置", 163, 405, 140, 42, ExtensionAction::Settings);
        } else if (kind_ == Extension::Timers) {
            button(controls_, "重置", 94, 350, 130, 48, ExtensionAction::Secondary);
            primary_ = button(controls_, "开始", 240, 350, 130, 48, ExtensionAction::Primary, 0, true);
        }
        lv_obj_add_flag(controls_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (state_.page == ExtensionPage::Presets) {
        const int presets[] = {1, 3, 5, 10, 15, 30};
        for (int i = 0; i < 6; ++i)
            button(content_, (std::to_string(presets[i]) + " 分钟").c_str(), 69 + i % 2 * 171, 144 + i / 2 * 72, 157,
                   60, ExtensionAction::Preset, presets[i]);
        button(content_, "常用时长", 69, 372, 157, 48, ExtensionAction::Preset, 0);
        button(content_, "自定义", 240, 372, 157, 48, ExtensionAction::Custom, 0, true);
    } else if (state_.page == ExtensionPage::Custom) {
        ui::text(content_, "时          分          秒", 0, 149, 466, &toolbox_misans_20, 0x98a4b7);
        for (int i = 0; i < 3; ++i) {
            const int x = 77 + 108 * i;
            button(content_, "+", x, 186, 96, 40, ExtensionAction::Plus, i);
            values_.push_back(ui::text(content_, "00", x, 247, 96, &toolbox_misans_36));
            button(content_, "−", x, 306, 96, 40, ExtensionAction::Minus, i);
        }
        button(content_, "保存常用", 94, 365, 130, 48, ExtensionAction::Save);
        button(content_, "使用", 240, 365, 130, 48, ExtensionAction::Confirm, 0, true);
    } else if (state_.page == ExtensionPage::Settings) {
        const char *focus[] = {"专注", "短休息", "长休息", "完成统计"};
        const char *level[] = {"提示音", "容差", "相对零点", "校准"};
        for (int i = 0; i < 4; ++i) {
            auto *b = button(content_, "", 69, 144 + i * 64, 328, 58, ExtensionAction::Edit, i);
            auto *name = ui::text(b, kind_ == Extension::Focus ? focus[i] : level[i], 19, 14, 170, &toolbox_misans_23);
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
            values_.push_back(ui::text(b, "", 190, 17, 120, &toolbox_misans_20, ui::accent));
        }
        if (kind_ == Extension::Focus) {
            button(content_, "−", 151, 405, 72, 42, ExtensionAction::Minus);
            button(content_, "+", 243, 405, 72, 42, ExtensionAction::Plus);
        }
    } else if (state_.page == ExtensionPage::Calibration) {
        ui::text(content_, "屏幕朝上，保持静止", 0, 161, 466, &toolbox_misans_23);
        ui::text(content_, "180°", 0, 226, 466, &toolbox_misans_36, ui::accent);
        message_ = ui::text(content_, "采集第一次后原地旋转", 60, 294, 346, &toolbox_misans_20, 0x98a4b7);
        button(content_, "采集", 143, 358, 180, 61, ExtensionAction::Calibrate, 0, true);
    } else if (state_.page == ExtensionPage::City) {
        input_ = lv_textarea_create(content_);
        lv_obj_set_pos(input_, 69, 144);
        lv_obj_set_size(input_, 328, 48);
        lv_obj_set_style_text_font(input_, &toolbox_misans_20, 0);
        lv_textarea_set_one_line(input_, true);
        lv_textarea_set_max_length(input_, 48);
        lv_textarea_set_placeholder_text(input_, "城市名称 / 拼音");
        keyboard_ = lv_keyboard_create(content_);
        auto *keyboard = keyboard_;
        lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(keyboard, 45, 300);
        lv_obj_set_size(keyboard, 376, 132);
        lv_obj_set_style_text_font(keyboard, &toolbox_misans_18, 0);
        lv_keyboard_set_textarea(keyboard, input_);
        static const char *const keys[] = {"1", "2", "3", "4", "5", "6", "7",  "8",  "9",     "0",  "\n",
                                           "q", "w", "e", "r", "t", "y", "u",  "i",  "o",     "p",  "\n",
                                           "a", "s", "d", "f", "g", "h", "j",  "k",  "l",     "\n", "z",
                                           "x", "c", "v", "b", "n", "m", "\n", "BS", "Space", "OK", ""};
        static const auto widths = [] {
            std::array<lv_buttonmatrix_ctrl_t, 39> widths{};
            widths.fill(static_cast<lv_buttonmatrix_ctrl_t>(1));
            widths[37] = static_cast<lv_buttonmatrix_ctrl_t>(5);
            return widths;
        }();
        lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, keys, widths.data());
        lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
        lv_obj_add_event_cb(
            keyboard,
            [](lv_event_t *e) {
                auto *self = static_cast<ExtensionView *>(lv_event_get_user_data(e));
                auto *keyboard = static_cast<lv_obj_t *>(lv_event_get_target(e));
                const auto selected = lv_buttonmatrix_get_selected_button(keyboard);
                if (selected == LV_BUTTONMATRIX_BUTTON_NONE)
                    return;
                const std::string key = lv_buttonmatrix_get_button_text(keyboard, selected);
                if (key == "BS")
                    lv_textarea_delete_char(self->input_);
                else if (key == "Space")
                    lv_textarea_add_text(self->input_, " ");
                else if (key != "OK")
                    lv_textarea_add_text(self->input_, key.c_str());
                if (self->pending_.action == ExtensionAction::None) {
                    self->pending_.action = key == "OK" ? ExtensionAction::Search : ExtensionAction::Input;
                    self->pending_.text = lv_textarea_get_text(self->input_);
                }
            },
            LV_EVENT_VALUE_CHANGED, this);
        lv_obj_set_style_bg_color(input_, lv_color_hex(0x202631), 0);
        lv_obj_set_style_text_color(input_, lv_color_hex(0xf3f6fc), 0);
        lv_obj_set_style_border_width(input_, 0, 0);
        lv_obj_set_style_radius(input_, 24, 0);
        lv_obj_set_style_bg_opa(keyboard, 0, 0);
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x202631), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(keyboard, 255, LV_PART_ITEMS);
        lv_obj_set_style_text_color(keyboard, lv_color_hex(0xf3f6fc), LV_PART_ITEMS);
        lv_obj_set_style_radius(keyboard, 8, LV_PART_ITEMS);
        lv_obj_add_event_cb(
            input_,
            [](lv_event_t *e) {
                auto *self = static_cast<ExtensionView *>(lv_event_get_user_data(e));
                lv_obj_remove_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN);
                if (self->results_)
                    lv_obj_add_flag(self->results_, LV_OBJ_FLAG_HIDDEN);
            },
            LV_EVENT_FOCUSED, this);
        button(content_, "搜索", 257, 250, 140, 42, ExtensionAction::Search, 0, true);
        message_ = ui::text(content_, "", 69, 203, 328, &toolbox_misans_20, ui::accent);
    } else if (state_.page == ExtensionPage::Details || state_.page == ExtensionPage::Stats) {
        message_ = ui::text(content_, "", 65, 145, 336, &toolbox_misans_23, ui::accent);
        detail_ = ui::text(content_, "", 65, 194, 336, &toolbox_misans_20);
        lv_obj_set_style_text_line_space(detail_, 14, 0);
    }
}

void ExtensionView::confirmation() {
    if (dialog_) {
        lv_obj_delete(dialog_);
        dialog_ = nullptr;
    }
    if (!state_.confirm)
        return;
    dialog_ = ui::panel(root_, 0, 0, 466, 466);
    lv_obj_set_style_bg_opa(dialog_, 165, 0);
    lv_obj_add_flag(dialog_, LV_OBJ_FLAG_CLICKABLE);
    auto *card = ui::panel(dialog_, 31, 132, 404, 202, 0x2b2b2b, 58);
    lv_obj_set_style_border_color(card, lv_color_hex(0x6a6a6a), 0);
    lv_obj_set_style_border_width(card, 3, 0);
    ui::text(card, kind_ == Extension::Counter ? "确定重置计数？" : "退出专注？", 0, 23, 398, &toolbox_misans_26);
    if (kind_ == Extension::Focus)
        ui::text(card, "再次按 KEYA 退出专注", 0, 68, 398, &toolbox_misans_20);
    button(card, "取消", kind_ == Extension::Counter ? 38 : 128, 122, 148, 63, ExtensionAction::Cancel);
    if (kind_ == Extension::Counter)
        button(card, "确定重置", 218, 122, 148, 63, ExtensionAction::Confirm, 0, true);
}

void ExtensionView::update(const ExtensionViewState &state, uint32_t now) {
    const bool level_changed = !built_ || state.valid != state_.valid || state.angle != state_.angle ||
                               state.direction != state_.direction || state.error != state_.error ||
                               state.pin != state_.pin || state.tolerance != state_.tolerance;
    state_ = state;
    if (!built_ || built_page_ != state.page) {
        built_page_ = state.page;
        built_ = true;
        build();
    }
    if (built_confirm_ != state.confirm) {
        built_confirm_ = state.confirm;
        confirmation();
    }
    if (controls_ && !lv_obj_has_flag(controls_, LV_OBJ_FLAG_HIDDEN)) {
        const uint32_t age = now - revealed_at_;
        if (age >= 2300)
            lv_obj_add_flag(controls_, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_set_style_opa(controls_, age <= 2000 ? 255 : 255 * (2300 - age) / 300, 0);
    }
    if (state.page == ExtensionPage::Main) {
        if (kind_ == Extension::Counter) {
            label(time_, std::to_string(state.count));
            lv_obj_align(time_, LV_ALIGN_CENTER, 0, 0);
            const auto *font = state.count < 100000      ? &toolbox_misans_98
                               : state.count < 100000000 ? &toolbox_misans_65
                                                         : &toolbox_misans_36;
            if (lv_obj_get_style_text_font(time_, LV_PART_MAIN) != font)
                lv_obj_set_style_text_font(time_, font, 0);
        } else if (kind_ == Extension::Timers || kind_ == Extension::Focus) {
            label(time_, state.time);
            label(detail_, state.detail);
            lv_arc_set_value(ring_, std::clamp(static_cast<int>(state.progress * 1000), 0, 1000));
            const char *action = state.timer == Countdown::State::Running  ? "暂停"
                                 : state.timer == Countdown::State::Paused ? "继续"
                                 : state.timer == Countdown::State::Done   ? "再来一次"
                                                                           : "开始";
            if (kind_ == Extension::Focus)
                action = state.timer == Countdown::State::Running ? "退出专注" : state.rest ? "开始休息" : "开始";
            label(lv_obj_get_child(primary_, 0), action);
        } else if (kind_ == Extension::Level) {
            label(time_, state.valid ? number(state.angle) + "°" : "--.--°");
            label(message_, state.frozen ? "HOLD" : state.pin ? "PIN " + number(state.target) + "°" : "PINPOINT");
            auto signed_angle = [](double value) {
                char text[24];
                snprintf(text, sizeof(text), "%+.2f", value);
                return std::string(text);
            };
            label(detail_,
                  state.valid ? "X " + signed_angle(state.x) + "°   Y " + signed_angle(state.y) + "°" : state.message);
        } else {
            const auto &day = state.weather.days[state.day];
            label(message_, state.weather.city.name.empty() ? "请在配网时配置城市" : state.weather.city.name);
            label(detail_, std::string(state.day == 0 ? "今日" : "明日") + " " + day.date +
                               (state.weather.cached ? " · 缓存" : ""));
            label(time_, state.day == 0
                             ? (day.temperature.empty() ? "--°" : day.temperature + "°")
                             : (day.low.empty() || day.high.empty() ? "--°" : day.low + "~" + day.high + "°"));
            const auto *font = state.day == 0 ? &toolbox_misans_65 : &toolbox_misans_36;
            if (lv_obj_get_style_text_font(time_, LV_PART_MAIN) != font)
                lv_obj_set_style_text_font(time_, font, 0);
            label(values_[0], day.condition.empty() ? state.weather.message : day.condition);
            label(values_[1], "最高 " + (day.high.empty() ? "--" : day.high) + "°   最低 " +
                                  (day.low.empty() ? "--" : day.low) + "°");
            label(values_[2], "湿度 " + (day.humidity.empty() ? "--" : day.humidity) + "%   风速 " +
                                  (day.wind.empty() ? "--" : day.wind) + " km/h");
            label(values_[3], day.probability.empty()
                ? (day.precipitation.empty() ? "更新 " + day.updated : "降水 " + day.precipitation + " mm")
                : "降水概率 " + day.probability + "%");
            if (previous_day_ != state.day) {
                previous_day_ = state.day;
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, content_);
                lv_anim_set_values(&a, 80, 255);
                lv_anim_set_duration(&a, 180);
                lv_anim_set_exec_cb(&a,
                                    [](void *o, int32_t v) { lv_obj_set_style_opa(static_cast<lv_obj_t *>(o), v, 0); });
                lv_anim_start(&a);
            }
        }
    } else if (state.page == ExtensionPage::Custom) {
        for (int i = 0; i < 3; ++i) {
            char value[8];
            snprintf(value, sizeof(value), "%02d", state.values[i]);
            label(values_[i], value);
        }
    } else if (state.page == ExtensionPage::Settings) {
        if (kind_ == Extension::Focus) {
            for (int i = 0; i < 3; ++i)
                label(values_[i], std::to_string(state.values[i]) + " 分钟" + (state.edit == i ? " ‹" : ""));
            label(values_[3], "查看 ›");
        } else {
            label(values_[0], state.sound ? "开启" : "关闭");
            label(values_[1], number(state.tolerance) + "°");
            label(values_[2], "清除");
            label(values_[3], "进入 ›");
        }
    } else if (state.page == ExtensionPage::Calibration)
        label(message_, state.message);
    else if (state.page == ExtensionPage::City) {
        label(message_, state.weather.message);
        if (city_revision_ != state.weather.revision) {
            city_revision_ = state.weather.revision;
            if (results_) {
                lv_obj_delete(results_);
                results_ = nullptr;
            }
            if (state.weather.cities.empty()) {
                lv_obj_remove_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
                results_ = ui::panel(content_, 69, 300, 328, 120);
                auto *list = results_;
                lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_scroll_dir(list, LV_DIR_VER);
                for (size_t i = 0; i < state.weather.cities.size(); ++i)
                    button(list, state.weather.cities[i].name.c_str(), 0, i * 55, 328, 50, ExtensionAction::City, i);
            }
        }
    } else if (state.page == ExtensionPage::Details) {
        const auto &day = state.weather.days[state.day];
        label(message_, state.weather.message);
        label(detail_, "湿度 " + day.humidity + "%\n风速 " + day.wind + " km/h\n更新 " + day.updated + "\n" +
                           (state.day == 0 ? "实时天气" : "明日预报") + "\n来源 UAPI");
    } else if (state.page == ExtensionPage::Stats) {
        label(message_, "已完成专注");
        label(detail_, std::to_string(state.completed) + " 次");
    }
    const bool animated_weather = kind_ == Extension::Weather && state.weather.days[state.day].valid;
    if (drawing_ && now - frame_at_ >= 33 && (level_changed || animated_weather)) {
        frame_at_ = now;
        lv_obj_invalidate(drawing_);
    }
}

void ExtensionView::draw(lv_event_t *event) {
    auto *self = static_cast<ExtensionView *>(lv_event_get_user_data(event));
    auto *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_target(event)), &area);
    auto rect = [&](int x, int y, int w, int h, uint32_t color, int radius, int opacity = 255) {
        lv_draw_rect_dsc_t desc;
        lv_draw_rect_dsc_init(&desc);
        desc.bg_color = lv_color_hex(color);
        desc.bg_opa = opacity;
        desc.radius = radius;
        lv_area_t box = {area.x1 + x, area.y1 + y, area.x1 + x + w - 1, area.y1 + y + h - 1};
        lv_draw_rect(layer, &desc, &box);
    };
    auto line = [&](double x, double y, double xx, double yy, uint32_t color, int width) {
        lv_draw_line_dsc_t desc;
        lv_draw_line_dsc_init(&desc);
        desc.color = lv_color_hex(color);
        desc.width = width;
        desc.p1 = {static_cast<lv_value_precise_t>(area.x1 + x), static_cast<lv_value_precise_t>(area.y1 + y)};
        desc.p2 = {static_cast<lv_value_precise_t>(area.x1 + xx), static_cast<lv_value_precise_t>(area.y1 + yy)};
        lv_draw_line(layer, &desc);
    };
    const auto &state = self->state_;
    const double t = lv_tick_get() / 1000.;
    if (self->kind_ == Extension::Level) {
        const uint32_t bg =
            state.valid && state.error <= state.tolerance ? (state.pin ? 0x185fc8 : 0x168e49) : 0x050505;
        rect(0, 0, 466, 466, bg, 233);
        for (int angle = 0; angle < 360; angle += 5) {
            const double a = angle * .017453293;
            const int r = angle % 30 ? 205 : 199;
            line(233 + r * cos(a), 233 + r * sin(a), 233 + 214 * cos(a), 233 + 214 * sin(a), 0xaab5ba, 2);
        }
        lv_draw_arc_dsc_t arc;
        lv_draw_arc_dsc_init(&arc);
        arc.color = lv_color_white();
        arc.width = 7;
        arc.radius = 171;
        arc.center = {area.x1 + 233, area.y1 + 233};
        arc.start_angle = 205;
        arc.end_angle = 335;
        lv_draw_arc(layer, &arc);
        arc.start_angle = 25;
        arc.end_angle = 155;
        lv_draw_arc(layer, &arc);
        if (state.valid && state.error > state.tolerance) {
            const double a = state.direction * .017453293;
            const double x = 233 + 146 * cos(a), y = 233 + 146 * sin(a);
            lv_draw_triangle_dsc_t triangle;
            lv_draw_triangle_dsc_init(&triangle);
            triangle.color = lv_color_white();
            triangle.p[0] = {static_cast<lv_value_precise_t>(area.x1 + x),
                             static_cast<lv_value_precise_t>(area.y1 + y)};
            triangle.p[1] = {static_cast<lv_value_precise_t>(area.x1 + x - 20 * cos(a - .5)),
                             static_cast<lv_value_precise_t>(area.y1 + y - 20 * sin(a - .5))};
            triangle.p[2] = {static_cast<lv_value_precise_t>(area.x1 + x - 20 * cos(a + .5)),
                             static_cast<lv_value_precise_t>(area.y1 + y - 20 * sin(a + .5))};
            lv_draw_triangle(layer, &triangle);
        }
        return;
    }
    const auto &day = state.weather.days[state.day];
    if (!day.valid) {
        rect(0, 0, 466, 466, 0, 0);
        return;
    }
    const int code = day.code;
    const bool clear = code == 100 || code == 150, partly = code == 102 || code == 103 || code == 151 || code == 153;
    const bool snow = code >= 400 && code < 500, wet = code >= 300 && code < 400, fog = code >= 500 && code < 520;
    // A four-pixel repeating Bayer pattern distributes RGB565 quantization
    // error without a full-screen buffer or animated noise.
    static uint16_t pixels[466 * 4];
    static lv_image_dsc_t sky_image = {};
    static uint32_t previous_top = UINT32_MAX;
    const uint32_t top = day.night ? 0x070f26 : clear || partly ? 0x145b92 : 0x273d52;
    const uint32_t bottom = day.night ? 0x152943 : clear || partly ? 0x558eae : 0x111b29;
    if (previous_top != top) {
        const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        for (int y = 0; y < 466; ++y) {
            for (int x = 0; x < 4; ++x) {
                unsigned pixel = 0;
                for (int channel = 0; channel < 3; ++channel) {
                    const int shift = 16 - channel * 8;
                    const int start = (top >> shift) & 255;
                    const int end = (bottom >> shift) & 255;
                    const float value = start + (end - start) * y / 465.0f;
                    const int levels = channel == 1 ? 63 : 31;
                    const int quantized = std::clamp(static_cast<int>(value * levels / 255.0f +
                        (bayer[y % 4][x] + .5f) / 16.0f), 0, levels);
                    pixel = (pixel << (channel == 1 ? 6 : 5)) | quantized;
                }
                pixels[y * 4 + x] = pixel;
            }
        }
        sky_image.header.magic = LV_IMAGE_HEADER_MAGIC;
        sky_image.header.cf = LV_COLOR_FORMAT_RGB565;
        sky_image.header.w = 4;
        sky_image.header.h = 466;
        sky_image.header.stride = 8;
        sky_image.data_size = sizeof(pixels);
        sky_image.data = reinterpret_cast<const uint8_t*>(pixels);
        previous_top = top;
        lv_image_cache_drop(&sky_image);
    }
    lv_draw_image_dsc_t sky;
    lv_draw_image_dsc_init(&sky);
    sky.src = &sky_image;
    sky.tile = true;
    lv_draw_image(layer, &sky, &area);
    if (clear || partly) {
        for (int r = 110; r >= 28; r -= 10)
            rect(330 - r, 99 - r, r * 2, r * 2, day.night ? 0xdce9ff : 0xffe9ad, r, 8);
        rect(302, 71, 56, 56, day.night ? 0xe3edff : 0xfff2c2, 28);
    }
    if (day.night)
        for (int i = 0; i < 24; ++i)
            rect((i * 137) % 466, (i * 83) % 290, 2, 2, 0xdce8ff, 1, 100 + 50 * sin(t + i));
    if (!clear)
        for (int i = 0; i < (fog ? 7 : 5); ++i) {
            const int x = static_cast<int>(i * 143 + t * (4 + i)) % 760 - 150, y = 55 + i * (fog ? 50 : 44);
            rect(x, y, 250, 62, 0xafc2d6, 31, 30);
            rect(x + 22, y - 20, 140, 70, 0xafc2d6, 35, 20);
        }
    if (wet || snow)
        for (int i = 0; i < 48; ++i) {
            const int speed = 65 + i % 9 * 17;
            const bool flake = snow || (code == 313 && i % 2 == 0);
            const int y = static_cast<int>(i * 83.17 + t * (flake ? speed * .22 : speed * 1.5)) % 580 - 60;
            const int x = (static_cast<int>(i * 137.51 + (flake ? sin(t * .7 + i) * 13 : -t * 20)) % 466 + 466) % 466;
            if (flake || code == 304)
                rect(x, y, 3 + i % 3, 3 + i % 3, 0xe6f2ff, 3, 160);
            else
                line(x, y, x - 4, y + (code == 309 ? 8 : 20), 0x8eacc3, code >= 307 ? 2 : 1);
        }
    if ((code == 302 || code == 303 || code == 304) && fmod(t, 9) < .15) {
        rect(0, 0, 466, 466, 0xbdcfff, 0, 50);
        line(343, 48, 320, 91, 0xdce8ff, 2);
        line(320, 91, 341, 88, 0xdce8ff, 2);
        line(341, 88, 313, 134, 0xdce8ff, 2);
    }
}
} // namespace toolbox
