#include "app_lighting.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_random.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <src/misc/lv_text_private.h>
#include <system/app_icons.h>
#include <system/ble.h>
#include <system/ui.h>

using namespace toolbox;
using namespace toolbox::lighting;
namespace {
constexpr Brand brands[] = {Brand::Inkee,   Brand::Yongnuo, Brand::Sidus,  Brand::Lyd,
                            Brand::Godox,   Brand::Lumin,   Brand::Jinbei, Brand::Xsj,
                            Brand::Nanlite, Brand::Ulanzi,  Brand::Zhiyun};
constexpr uint32_t panel_color = 0x151e27;
constexpr int fixture_list_y = 120;
constexpr int fixture_list_height = 258;

std::string display_name(const Device &device, const lv_font_t &font) {
    const std::string raw(device.name, std::find(std::begin(device.name), std::end(device.name), '\0'));
    std::string name;
    for (uint32_t offset = 0; offset < raw.size();) {
        const uint32_t start = offset;
        const uint32_t letter = lv_text_encoded_next(raw.c_str(), &offset);
        if (offset == start)
            ++offset;
        if (letter >= 0xff01 && letter <= 0xff5e) {
            name += static_cast<char>(letter - 0xfee0);
        } else if (letter == 0x3000) {
            name += ' ';
        } else if (letter >= 32 && letter != 127) {
            lv_font_glyph_dsc_t glyph{};
            if (lv_font_get_glyph_dsc(&font, &glyph, letter, 0) && !glyph.is_placeholder)
                name.append(raw, start, offset - start);
        }
    }
    const auto first = name.find_first_not_of(' ');
    if (first != std::string::npos)
        return name.substr(first, name.find_last_not_of(' ') - first + 1);

    char suffix[8];
    std::snprintf(suffix, sizeof(suffix), " %02X%02X", unsigned(device.address[4]), unsigned(device.address[5]));
    return std::string(brand_name(device.brand)) + suffix;
}

int temperature_step(Brand brand) {
    switch (brand) {
    case Brand::Lyd:
    case Brand::Godox:
    case Brand::Jinbei:
    case Brand::Zhiyun:
        return 100;
    default:
        return 50;
    }
}
} // namespace
AppLighting::AppLighting() {
    setAppInfo().name = "灯光控制";
    setAppInfo().icon = (void *)&toolbox_app_lighting;
}
void AppLighting::save() {
    if (!dirty_)
        return;
    Settings s("lighting", true);
    s.SetBlob("fixtures", &data_, sizeof(data_));
    dirty_ = false;
}
void AppLighting::onOpen() {
    data_ = {};
    Settings s("lighting");
    Store stored;
    size_t n = sizeof(stored);
    if (s.GetBlob("fixtures", &stored, &n) == ESP_OK && n == sizeof(stored) && stored.version == 1 &&
        stored.count <= 8)
        data_ = stored;
    if (!data_.controller) {
        data_.controller = 1 + esp_random() % 0xfffe;
        dirty_ = true;
        save();
    }
    page_ = Page::Lamps;
    selected_ = -1;
    shown_.clear();
    pending_action_ = 0;
    redraw_at_ = 0;
    reconnect_ = is_new_ = deferred_send_ = false;
    render();
}
void AppLighting::onClose() {
    deferred_send_ = false;
    lighting::stop();
    save();
    LvglLockGuard lock;
    if (root_)
        lv_obj_delete(root_);
    root_ = nullptr;
    pending_action_ = 0;
}
void AppLighting::event(lv_event_t *e) {
    auto *self = static_cast<AppLighting *>(lv_event_get_user_data(e));
    auto *obj = static_cast<lv_obj_t *>(lv_event_get_target(e));
    uintptr_t code = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj));
    auto a = Action(code & 255);
    int v = int(code >> 8) - 1024;
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
        v = lv_slider_get_value(obj);
    self->pending_action_ = uint32_t((v + 1024) * 256 + a);
}
lv_obj_t *AppLighting::button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, Action a,
                              int value, bool accent) {
    auto *b = ui::button(
        parent, text, x, y, w, h, [](lv_event_t *) {}, accent ? ui::accent : panel_color, &toolbox_misans_lighting_20);
    bind_action(b, a, value);
    return b;
}
void AppLighting::bind_action(lv_obj_t* object, Action action, int value) {
    lv_obj_set_user_data(object, reinterpret_cast<void*>(uintptr_t((value + 1024) * 256 + int(action))));
    lv_obj_add_event_cb(object, event, LV_EVENT_CLICKED, this);
}
lv_obj_t *AppLighting::slider(int x, int y, int w, int min, int max, int value, Action a) {
    auto *o = lv_slider_create(root_);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, 16);
    lv_slider_set_range(o, min, max);
    lv_slider_set_value(o, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x29343e), LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, lv_color_hex(ui::accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(o, lv_color_hex(0xe4f0ff), LV_PART_KNOB);
    lv_obj_set_style_pad_all(o, 7, LV_PART_KNOB);
    lv_obj_set_user_data(o, reinterpret_cast<void *>(uintptr_t(1024 * 256 + int(a))));
    lv_obj_add_event_cb(o, event, LV_EVENT_VALUE_CHANGED, this);
    return o;
}
void AppLighting::menu_entry(const char* caption, int index, Action action, int value) {
    auto* row = ui::menu_row(list_, caption, 0, index * ui::menu_pitch).object;
    bind_action(row, action, value);
}
void AppLighting::render() {
    LvglLockGuard lock;
    if (root_)
        lv_obj_delete(root_);
    root_ = ui::round_page(lv_screen_active());
    status_ = list_ = power_ = v1_ = v2_ = v3_ = s1_ = s2_ = nullptr;
    auto header = [&](const char *title, bool fixture_list = false) {
        auto *name = ui::text(root_, title, 100, 47, 266, &toolbox_misans_lighting_26);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        const int x = fixture_list ? 64 : 60;
        const int y = fixture_list ? 72 : 85;
        auto *b = ui::icon_button(root_, &toolbox_icon_back_28, x, y, 44, 44, [](lv_event_t *) {});
        lv_obj_set_user_data(b, reinterpret_cast<void *>(uintptr_t(1024 * 256 + Back)));
        lv_obj_add_event_cb(b, event, LV_EVENT_CLICKED, this);
    };
    if (page_ == Page::Lamps) {
        header("灯光控制", true);
        list_ = ui::menu_list(root_, fixture_list_y, fixture_list_height);
        for (int i = 0; i < data_.count; i++)
            menu_entry(display_name(data_.lamps[i].device, toolbox_misans_23).c_str(), i, Select, i);
        if (!data_.count)
            ui::text(list_, "未添加灯具", 0, 105, ui::menu_width, &toolbox_misans_lighting_26, 0xa8b5c5);
        button(root_, "添加灯具", 125, 383, 216, 54, Add, 0, true);
    } else if (page_ == Page::Brands) {
        header("选择品牌", true);
        list_ = ui::menu_list(root_, fixture_list_y, 278);
        for (size_t i = 0; i < std::size(brands); i++)
            menu_entry(brand_name(brands[i]), i, Select, i);
        ui::cache_scroll_rows(list_);
    } else if (page_ == Page::Scan) {
        header(brand_name(brand_), true);
        status_ = ui::text(root_, "正在扫描", 108, 88, 250, &toolbox_misans_lighting_20, 0xa8b5c5);
        lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
        list_ = ui::menu_list(root_, fixture_list_y, fixture_list_height);
        button(root_, "重新扫描", 125, 383, 216, 54, Refresh);
        shown_.clear();
    } else if (page_ == Page::Address) {
        header(brand_name(brand_));
        if (brand_ == Brand::Inkee) {
            ui::text(root_, "将灯具切换到配对模式", 56, 146, 354, &toolbox_misans_lighting_20);
            button(root_, "创建灯位", 126, 226, 214, 68, Confirm, 0, true);
        } else {
            ui::text(root_, "频道", 100, 135, 266);
            v1_ = ui::text(root_, "", 138, 185, 190, &toolbox_misans_36);
            button(root_, "-", 80, 180, 55, 52, Channel, -1);
            button(root_, "+", 331, 180, 55, 52, Channel, 1);
            ui::text(root_, "分组", 100, 248, 266, &toolbox_misans_lighting_26);
            v2_ = ui::text(root_, "", 138, 285, 190, &toolbox_misans_36);
            button(root_, "-", 80, 279, 55, 52, Group, -1);
            button(root_, "+", 331, 279, 55, 52, Group, 1);
            button(root_, "确认", 146, 369, 174, 45, Confirm, 0, true);
        }
    } else if (page_ == Page::Channel) {
        header("灯具频道");
        ui::text(root_, "CH", 100, 145, 266, &toolbox_misans_lighting_26);
        v1_ = ui::text(root_, "", 128, 190, 210, &toolbox_misans_65);
        button(root_, "-", 69, 211, 65, 59, Channel, -1);
        button(root_, "+", 332, 211, 65, 59, Channel, 1);
        s1_ = slider(96, 302, 274, 1, 255, temp_.channel ? temp_.channel : 1, Channel);
        button(root_, "确认", 146, 369, 174, 45, Confirm, 0, true);
    } else if (page_ == Page::Effects) {
        header("灯光模式");
        list_ = ui::menu_list(root_, 132, 230);
        int row = 0;
        if (temp_.caps.cct || !temp_.caps.hsi) menu_entry("CCT", row++, Cct);
        if (temp_.caps.hsi) menu_entry("HSI", row++, Color);
        for (const auto& fx : effects_for(brand_, temp_.product, temp_.caps)) menu_entry(fx.name, row++, Effect, fx.code);
    } else if (page_ == Page::Delete) {
        header("移除灯具");
        ui::text(root_, display_name(temp_, toolbox_misans_lighting_26).c_str(), 64, 180, 338, &toolbox_misans_lighting_26);
        button(root_, "取消", 89, 270, 135, 60, Back);
        button(root_, "移除", 242, 270, 135, 60, Confirm, 0, true);
    } else if (page_ == Page::Control) {
        header(display_name(temp_, toolbox_misans_lighting_26).c_str());
        render_control();
    } else {
        header(display_name(temp_, toolbox_misans_lighting_26).c_str());
        status_ = ui::text(root_, "", 112, 91, 254, &toolbox_misans_lighting_20, 0xa8b5c5);
        lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
        auto *label = ui::text(root_, "色相", 72, 130, 115, &toolbox_misans_lighting_20, 0xa8b5c5);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        v1_ = ui::text(root_, "", 205, 125, 187, &toolbox_misans_32);
        s1_ = slider(86, 173, 294, 0, 359, desired_.hue, Hue);
        label = ui::text(root_, "饱和度", 72, 207, 125, &toolbox_misans_lighting_20, 0xa8b5c5);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        v2_ = ui::text(root_, "", 205, 202, 187, &toolbox_misans_32);
        s2_ = slider(86, 249, 294, 0, 100, desired_.saturation, Saturation);
        if (brand_ == Brand::Yongnuo && temp_.variant != YnVariant::Classic360)
            button(root_, "设置 CH", 164, 276, 138, 42, Channel, 0);
        else if (brand_ == Brand::Inkee)
            button(root_, "配对", 164, 276, 138, 42, Pair);
        else
            button(root_, "重发", 164, 276, 138, 42, Resend);
        power_ = button(root_, desired_.power ? "关闭" : "开启", 87, 331, 135, 55, Power, 0, true);
        if (temp_.caps.hsi || temp_.caps.fx)
            button(root_, desired_.mode == Mode::Fx ? "特效" : desired_.mode == Mode::Hsi ? "HSI" : "CCT", 244, 331, 135, 55, ModeMenu);
        else
            button(root_, "移除", 244, 331, 135, 55, Remove);
        if (temp_.caps.hsi || temp_.caps.fx) {
            button(root_, "移除", 186, 397, 94, 31, Remove);
            if(brand_==Brand::Yongnuo && temp_.variant != YnVariant::Classic360)
                button(root_, "CH", 105, 397, 68, 31, Channel);
        }
    }
    render_values();
}
void AppLighting::render_control() {
    status_ = ui::text(root_, "", 108, 94, 242, &toolbox_misans_lighting_20, 0xa8b5c5);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
    button(root_, "移除", 351, 85, 62, 44, Remove);

    const bool variable_cct = temp_.caps.cct && temp_.caps.min_kelvin < temp_.caps.max_kelvin;
    constexpr int ruler_x = 78;
    const int brightness_y = variable_cct ? 128 : 166;
    brightness_.create_brightness(root_, ruler_x, brightness_y, desired_.brightness, temp_.caps.decimal,
                                  [](void *context, int value) {
                                      auto *app = static_cast<AppLighting *>(context);
                                      app->pending_action_ = uint32_t((value + 1024) * 256 + Brightness);
                                  }, this);
    if (variable_cct) {
        temperature_.create_temperature(root_, ruler_x, 229, desired_.kelvin, temp_.caps.min_kelvin,
                                        temp_.caps.max_kelvin, temperature_step(brand_),
                                        [](void *context, int value) {
                                            auto *app = static_cast<AppLighting *>(context);
                                            app->pending_action_ = uint32_t((value + 1024) * 256 + Kelvin);
                                        }, this);
    }

    const int actions_y = variable_cct ? 329 : 275;
    const bool tint = temp_.caps.min_tint < temp_.caps.max_tint;
    const bool pair = brand_ == Brand::Inkee;
    const bool channel = brand_ == Brand::Yongnuo && temp_.variant != YnVariant::Classic360;
    if (tint) {
        const int x = pair || channel ? 75 : 139;
        button(root_, "-", x, actions_y, 46, 44, Tint, -1);
        v3_ = ui::text(root_, "", x + 47, actions_y + 11, 93, &toolbox_misans_lighting_20);
        button(root_, "+", x + 141, actions_y, 46, 44, Tint, 1);
    }
    if (pair || channel || !tint) {
        const int x = tint ? 287 : 173;
        button(root_, pair ? "配对" : channel ? "设置 CH" : "重发", x, actions_y, 120, 44,
               pair ? Pair : channel ? Channel : Resend);
    }

    const int footer_y = variable_cct ? 386 : 338;
    const bool modes = temp_.caps.hsi || temp_.caps.fx;
    power_ = button(root_, desired_.power ? "关闭" : "开启", modes ? 126 : 174, footer_y, modes ? 101 : 118,
                    48, Power, 0, true);
    if (modes)
        button(root_, desired_.mode == Mode::Fx ? "特效" : desired_.mode == Mode::Hsi ? "HSI" : "CCT",
               239, footer_y, 101, 48, ModeMenu);
}
void AppLighting::render_values() {
    if (page_ == Page::Address || page_ == Page::Channel) {
        if (v1_)
            ui::set_text_fmt(v1_, "%u", unsigned(temp_.channel));
        if (v2_)
            ui::set_text_fmt(v2_, "%c", 'A' + temp_.group);
        return;
    }
    if (page_ != Page::Control && page_ != Page::Color)
        return;
    if (page_ == Page::Control) {
        brightness_.set_value(desired_.brightness);
        temperature_.set_value(desired_.kelvin);
        if (v3_)
            ui::set_text_fmt(v3_, "G/M %+d", int(desired_.tint));
    } else {
        if (v1_)
            ui::set_text_fmt(v1_, "%u°", unsigned(desired_.hue));
        if (v2_)
            ui::set_text_fmt(v2_, "%u%%", unsigned(desired_.saturation));
        if (s1_)
            lv_slider_set_value(s1_, desired_.hue, LV_ANIM_OFF);
        if (s2_)
            lv_slider_set_value(s2_, desired_.saturation, LV_ANIM_OFF);
    }
    if (power_)
        ui::set_text(lv_obj_get_child(power_, 0), desired_.power ? "关闭" : "开启");
}
void AppLighting::begin_brand(Brand b) {
    deferred_send_ = false;
    brand_ = b;
    selected_ = -1;
    is_new_ = true;
    reconnect_ = false;
    temp_ = {};
    temp_.brand = b;
    desired_ = {};
    if (is_broadcast(b)) {
        page_ = Page::Address;
        temp_.channel = 1;
        temp_.group = b == Brand::Inkee ? data_.next_group : 0;
        temp_.controller = data_.controller;
        if (b == Brand::Lyd) {
            temp_.caps = {2500, 9900, 0, 0, true, true, true, false};
        }
        snprintf(temp_.name, sizeof(temp_.name), "%s %u", brand_name(b), unsigned(data_.count + 1));
        render();
        return;
    }
    lighting::start(b);
    page_ = Page::Scan;
    render();
}
void AppLighting::selected_device(const Device &d) {
    deferred_send_ = false;
    temp_ = d;
    lighting::connect(d);
    reconnect_ = false;
}
void AppLighting::connected(const RadioState &s) {
    temp_ = s.selected;
    if (is_new_) {
        if (data_.count >= 8) {
            lighting::stop();
            page_ = Page::Lamps;
            render();
            return;
        }
        selected_ = data_.count++;
        desired_ = {};
        desired_.kelvin = std::clamp<uint16_t>(5600, temp_.caps.min_kelvin, temp_.caps.max_kelvin);
        desired_.power = false;
        data_.lamps[selected_] = {temp_, desired_};
        is_new_ = false;
        dirty_ = true;
        save();
    } else {
        data_.lamps[selected_].device = temp_;
        dirty_ = true;
        save();
    }
    desired_.kelvin = std::clamp(desired_.kelvin, temp_.caps.min_kelvin, temp_.caps.max_kelvin);
    desired_.tint = std::clamp(desired_.tint, temp_.caps.min_tint, temp_.caps.max_tint);
    page_ = brand_ == Brand::Yongnuo && !temp_.channel ? Page::Channel : Page::Control;
    render();
}
void AppLighting::update_scan(const RadioState &s) {
    if (reconnect_ && s.phase == Phase::Scanning) {
        selected_device(temp_);
        return;
    }
    if (s.phase == Phase::Ready && s.selected_valid) {
        connected(s);
        return;
    }
    LvglLockGuard lock;
    if (status_)
        ui::set_text(status_, s.message.c_str());
    bool same = shown_.size() == s.devices.size();
    if (same)
        for (size_t i = 0; i < shown_.size(); i++)
            if (shown_[i].address != s.devices[i].address || strcmp(shown_[i].name, s.devices[i].name) != 0) {
                same = false;
                break;
            }
    if (same || !list_)
        return;
    shown_ = s.devices;
    lv_obj_clean(list_);
    for (size_t i = 0; i < shown_.size(); i++)
        menu_entry(display_name(shown_[i], toolbox_misans_23).c_str(), i, Select, i);
}
void AppLighting::change() {
    if (selected_ >= 0) {
        data_.lamps[selected_].state = desired_;
        dirty_ = true;
    }
    deferred_send_ = true;
    send_at_ = GetHAL().millis() + 100;
    LvglLockGuard lock;
    render_values();
}
void AppLighting::go_back() {
    if (page_ == Page::Lamps) {
        close();
        return;
    }
    if (page_ == Page::Color || page_ == Page::Channel || page_ == Page::Delete || page_ == Page::Effects) {
        page_ = Page::Control;
        render();
        return;
    }
    if (page_ == Page::Brands || page_ == Page::Control) {
        deferred_send_ = false;
        lighting::stop();
        save();
        page_ = Page::Lamps;
        selected_ = -1;
        render();
        return;
    }
    lighting::stop();
    page_ = Page::Brands;
    render();
}
void AppLighting::handle(Action a, int v) {
    if (a == Back) {
        go_back();
        return;
    }
    if (a == Add && page_ == Page::Lamps) {
        if (data_.count >= 8)
            return;
        page_ = Page::Brands;
        render();
        return;
    }
    if (a == Select) {
        if (page_ == Page::Brands && v >= 0 && size_t(v) < std::size(brands)) {
            begin_brand(brands[v]);
            return;
        }
        if (page_ == Page::Lamps && v >= 0 && v < data_.count) {
            selected_ = v;
            temp_ = data_.lamps[v].device;
            desired_ = data_.lamps[v].state;
            brand_ = temp_.brand;
            is_new_ = false;
            lighting::start(brand_);
            if (is_broadcast(brand_)) {
                lighting::connect(temp_);
                page_ = Page::Control;
                render();
            } else {
                reconnect_ = true;
                page_ = Page::Scan;
                render();
            }
            return;
        }
        if (page_ == Page::Scan && v >= 0 && size_t(v) < shown_.size()) {
            selected_device(shown_[v]);
            return;
        }
    }
    if (a == Refresh && page_ == Page::Scan) {
        reconnect_ = !is_new_;
        lighting::start(brand_);
        render();
        return;
    }
    if (page_ == Page::Address) {
        if (a == Channel)
            temp_.channel = std::clamp<int>(temp_.channel + v, 1, 100);
        if (a == Group)
            temp_.group = std::clamp<int>(temp_.group + v, 0, 5);
        if (a == Confirm) {
            if (brand_ == Brand::Inkee) {
                if (data_.next_group == 255)
                    return;
                ++data_.next_group;
                dirty_ = true;
            }
            lighting::start(brand_);
            lighting::connect(temp_);
            auto current = lighting::status();
            if (current.selected_valid)
                connected(current);
            return;
        }
        LvglLockGuard lock;
        render_values();
        return;
    }
    if (page_ == Page::Channel) {
        if (a == Channel) {
            temp_.channel = std::clamp<int>(v == 1 || v == -1 ? int(temp_.channel) + v : v, 1, 255);
            LvglLockGuard lock;
            render_values();
            if (s1_)
                lv_slider_set_value(s1_, temp_.channel, LV_ANIM_OFF);
        } else if (a == Confirm) {
            lighting::set_channel(temp_.channel ? temp_.channel : 1);
            temp_.channel = temp_.channel ? temp_.channel : 1;
            data_.lamps[selected_].device = temp_;
            dirty_ = true;
            save();
            page_ = Page::Control;
            render();
        }
        return;
    }
    if (page_ == Page::Delete && a == Confirm && selected_ >= 0) {
        lighting::stop();
        for (int i = selected_; i + 1 < data_.count; i++)
            data_.lamps[i] = data_.lamps[i + 1];
        --data_.count;
        dirty_ = true;
        save();
        selected_ = -1;
        page_ = Page::Lamps;
        render();
        return;
    }
    if(page_==Page::Effects){
        if(a==Effect){desired_.mode=Mode::Fx;desired_.effect=v;page_=Page::Control;render();change();}
        else if(a==Color){desired_.mode=Mode::Hsi;page_=Page::Color;render();change();}
        else if(a==Cct){desired_.mode=Mode::Cct;page_=Page::Control;render();change();}
        return;
    }
    if (page_ != Page::Control && page_ != Page::Color)
        return;
    switch (a) {
    case ModeMenu:
        page_=Page::Effects;render();return;
    case Pair:
        lighting::pair_inkee();
        return;
    case Resend:
        lighting::send(desired_, true);
        return;
    case Power:
        desired_.power = !desired_.power;
        break;
    case Brightness:
        desired_.brightness = std::clamp(v, 0, 1000);
        if (!temp_.caps.decimal)
            desired_.brightness = ((desired_.brightness + 5) / 10) * 10;
        break;
    case Kelvin:
        desired_.kelvin = std::clamp<int>(v, temp_.caps.min_kelvin, temp_.caps.max_kelvin);
        if(desired_.mode!=Mode::Fx)desired_.mode = Mode::Cct;
        break;
    case Hue:
        desired_.hue = std::clamp(v, 0, 359);
        desired_.mode = Mode::Hsi;
        break;
    case Saturation:
        desired_.saturation = std::clamp(v, 0, 100);
        desired_.mode = Mode::Hsi;
        break;
    case Tint:
        desired_.tint = std::clamp<int>(desired_.tint + v, temp_.caps.min_tint, temp_.caps.max_tint);
        break;
    case Color:
        desired_.mode = Mode::Hsi;
        page_ = Page::Color;
        render();
        break;
    case Cct:
        desired_.mode = Mode::Cct;
        page_ = Page::Control;
        render();
        break;
    case Channel:
        page_ = Page::Channel;
        render();
        return;
    case Remove:
        page_ = Page::Delete;
        render();
        return;
    default:
        return;
    }
    change();
}
void AppLighting::onRunning() {
    auto key = keys_.update();
    if (key == input::KeyEvent::GoHome) {
        close();
        return;
    }
    const uint32_t pending = pending_action_.exchange(0);
    auto a = Action(pending & 255);
    int v = int(pending >> 8) - 1024;
    if (a != None)
        handle(a, v);
    else if (page_ == Page::Control && (key == input::KeyEvent::GoPrevious || key == input::KeyEvent::GoNext))
        handle(
            Brightness,
            std::clamp<int>(desired_.brightness + (key == input::KeyEvent::GoPrevious ? -50 : 50), 0, 1000));
    lighting::tick();
    uint32_t now = GetHAL().millis();
    if (deferred_send_ && int32_t(now - send_at_) >= 0) {
        deferred_send_ = false;
        lighting::send(desired_);
    }
    if (now - redraw_at_ < 100)
        return;
    redraw_at_ = now;
    auto s = lighting::status();
    if (page_ == Page::Scan) {
        update_scan(s);
    } else if ((page_ == Page::Control || page_ == Page::Color) && status_) {
        LvglLockGuard lock;
        ui::set_text(status_, s.message.c_str());
    }
}
