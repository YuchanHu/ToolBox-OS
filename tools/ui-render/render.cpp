#include <system/shell.h>
#include <system/touch_input.h>
#include <system/runtime.h>
#include <system/ble.h>
#include <system/app_icons.h>
#include <system/stopwatch_ui.h>
#include <apps/app_launcher/view/view.h>
#include <apps/app_stopwatch/app_stopwatch.h>
#include <apps/app_camera/app_camera.h>
#include <hal/hal.h>
#include "sony_mock.h"
#include <hal/display_frame.h>
#include <hal/utils/settings/settings.h>
#include <src/misc/lv_text_private.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <iostream>
#include <vector>
#include <stdexcept>

bool preview_analog = false;
Hal hardware;
Hal& GetHAL() { return hardware; }
namespace toolbox {
Runtime& runtime() { static Runtime instance; return instance; }
bool Runtime::post(Command command)
{
    if (command.type == CommandType::Wifi) hardware.network.enabled = command.value;
    if (command.type == CommandType::Provision) hardware.network.configMode = command.value;
    if (command.type == CommandType::Face) preview_analog = command.value;
    return true;
}
bool ble_enabled() { return false; }
esp_err_t ble_error() { return ESP_OK; }
}
namespace toolbox::camera {
Status preview;
int commands = 0;
bool delay_movie_reply = false;
Mode last_command = Mode::Photo;
void start() { preview = {}; preview.phase = Phase::Scanning; preview.message = "正在扫描"; }
void stop() { preview.phase = Phase::Off; }
void tick() {}
Status status() { return preview; }
void connect(const Device& device) { preview.name = device.name; preview.phase = Phase::Ready; preview.message = "已连接"; }
void trigger(Mode mode) {
    if (preview.phase != Phase::Ready || preview.busy) return;
    ++commands;
    last_command = mode;
    if (mode == Mode::Movie) {
        if (delay_movie_reply) {
            preview.busy = true;
            preview.message = "等待相机录像状态";
            return;
        }
        preview.recording_known = true;
        preview.recording = !preview.recording;
        preview.recording_since = hardware.millis();
        preview.message = preview.recording ? "相机正在录像" : "相机已停止录像";
    } else preview.message = "快门指令已发送";
}
}
std::vector<uint32_t> framebuffer(468 * 466);
std::vector<uint16_t> draw_buffer(468 * 466), second_draw_buffer(468 * 466);
bool full_render = false;
bool lighting_render = false;
unsigned flushed_pixels = 0;
std::filesystem::path capture_dir;
toolbox::TouchHistory input_history;

void advance(unsigned milliseconds)
{
    for (unsigned count = 0; count < milliseconds;) {
        const unsigned step = std::min(10u, milliseconds - count);
        lv_tick_inc(step);
        lv_timer_handler();
        count += step;
    }
}
void require_glyphs(lv_obj_t* root);
void screenshot(const char* name)
{
    const std::string_view scene(name);
    if (scene.substr(0, 8) == "sony-v4-" || scene.substr(0, 9) == "lighting-")
        require_glyphs(lv_obj_get_child(lv_screen_active(), -1));
    lv_refr_now(nullptr);
    std::ofstream output(capture_dir / (std::string(name) + ".bgra"), std::ios::binary);
    output.write(reinterpret_cast<char*>(framebuffer.data()), framebuffer.size() * 4);
    if (!output) throw std::runtime_error("Could not save LVGL capture");
}
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check_display_transfer()
{
    toolbox::display::FrameRegions regions;
    regions.add({0, 0, 9, 1});
    regions.add({10, 0, 19, 1});
    regions.add({300, 350, 309, 351});
    require(regions.count == 2, "Adjacent windows should merge and distant windows should stay separate");
    constexpr int width = 468, height = 466;
    for (int stride : {width * 2, width * 2 + 16}) {
        std::vector<uint8_t> frame(stride * height, 0xa5);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const uint16_t color = static_cast<uint16_t>(y * width + x);
                frame[y * stride + x * 2] = color & 0xff;
                frame[y * stride + x * 2 + 1] = color >> 8;
            }
        }
        const auto original = frame;
        const lv_area_t inputs[] = {{0, 0, 467, 465}, {17, 13, 464, 462},
                                    {467, 465, 467, 465}, {0, 0, 0, 0}, {22, 4, 22, 40}};
        for (auto input : inputs) {
            const auto area = toolbox::display::aligned_area(input, width, height);
            require(area.x1 <= input.x1 && area.y1 <= input.y1 && area.x2 >= input.x2 && area.y2 >= input.y2,
                    "Aligned transfer must contain dirty pixels");
            require(area.x1 % 2 == 0 && area.y1 % 2 == 0 && area.x2 % 2 == 1 && area.y2 % 2 == 1,
                    "CO5300 transfer coordinates and size must be even");
            const int row_bytes = (area.x2 - area.x1 + 1) * 2;
            std::vector<uint8_t> output(row_bytes * 32 + 4, 0xcc);
            for (int first_y = area.y1; first_y <= area.y2; first_y += 32) {
                const int rows = std::min(32, area.y2 - first_y + 1);
                std::fill(output.begin(), output.end(), 0xcc);
                toolbox::display::pack_rows(output.data(), frame.data(), stride, area, first_y, rows);
                for (int row = 0; row < rows; ++row) {
                    for (int column = 0; column < row_bytes; column += 2) {
                        const int source = (first_y + row) * stride + area.x1 * 2 + column;
                        const int target = row * row_bytes + column;
                        require(output[target] == frame[source + 1] && output[target + 1] == frame[source],
                                "DMA staging must preserve RGB565 colors, stride and region offset");
                    }
                }
                require(output[rows * row_bytes] == 0xcc, "DMA staging overran final chunk");
                std::vector<uint8_t> dma(row_bytes * rows + 4, 0xcc);
                const lv_area_t staged_area = {0, 0, row_bytes / 2 - 1, rows - 1};
                toolbox::display::pack_rows(dma.data(), output.data(), row_bytes, staged_area, 0, rows, false);
                require(std::equal(dma.begin(), dma.end() - 4, output.begin()),
                        "Preconverted frame must reach DMA without another byte swap");
                require(dma[row_bytes * rows] == 0xcc, "Preconverted DMA copy overran buffer");
            }
        }
        require(frame == original, "DMA staging must leave LVGL's source framebuffer unchanged");
    }
}
void swipe(int x, int from, int to, int duration = 120)
{
    toolbox::shell_touch(true, x, from);
    for (int step = 1; step <= 12; ++step) {
        advance(duration / 12);
        toolbox::shell_touch(true, x, from + (to - from) * step / 12);
    }
    toolbox::shell_touch(false, x, to);
}
void menu_clearance()
{
    lv_obj_update_layout(lv_layer_top());
    auto* page = lv_obj_get_child(lv_layer_top(), 0);
    auto* back = lv_obj_get_child(page, 1);
    auto* content = lv_obj_get_child(page, 2);
    lv_area_t back_area, content_area;
    lv_obj_get_coords(back, &back_area);
    lv_obj_get_coords(content, &content_area);
    require(content_area.y1 >= back_area.y2 + 16, "Menu overlaps back button");
}
lv_obj_t* find_text(lv_obj_t* root, const char* value)
{
    if (lv_obj_check_type(root, &lv_label_class) && std::string(lv_label_get_text(root)) == value) return root;
    for (unsigned index = 0; index < lv_obj_get_child_count(root); ++index) {
        if (auto* result = find_text(lv_obj_get_child(root, index), value)) return result;
    }
    return nullptr;
}
void require_menu_row(lv_obj_t* root, const char* caption)
{
    auto* label = find_text(root, caption);
    require(label != nullptr, caption);
    auto* row = lv_obj_get_parent(label);
    lv_obj_update_layout(row);
    require(lv_obj_get_width(row) == toolbox::ui::menu_width && lv_obj_get_height(row) == toolbox::ui::menu_height &&
            lv_color_eq(lv_obj_get_style_bg_color(row, LV_PART_MAIN), lv_color_hex(toolbox::ui::menu_color)) &&
            lv_obj_get_style_text_font(label, LV_PART_MAIN) == &toolbox_misans_23, caption);
}
void click_text(const char* value)
{
    auto* caption = find_text(lv_layer_top(), value);
    require(caption != nullptr, value);
    auto* target = lv_obj_get_parent(caption);
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    require(target != nullptr, "Missing clickable menu item");
    lv_obj_send_event(target, LV_EVENT_CLICKED, nullptr);
    advance(40);
}
// A character missing from a label's font is drawn as a placeholder box.
void require_glyphs(lv_obj_t* root)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(root, &lv_label_class)) {
        const auto* font = lv_obj_get_style_text_font(root, LV_PART_MAIN);
        const char* value = lv_label_get_text(root);
        uint32_t offset = 0;
        while (const uint32_t letter = lv_text_encoded_next(value, &offset)) {
            lv_font_glyph_dsc_t glyph = {};
            if (letter != '\n' && (!lv_font_get_glyph_dsc(font, &glyph, letter, 0) || glyph.is_placeholder))
                throw std::runtime_error(std::string("Font lacks a glyph for visible text: ") + value);
        }
    }
    for (unsigned index = 0; index < lv_obj_get_child_count(root); ++index)
        require_glyphs(lv_obj_get_child(root, index));
}void check_extensions();
void check_scroll_cache()
{
    using namespace toolbox::ui;
    auto* root = panel(lv_screen_active(), 1, 0, 466, 466);
    auto* viewport = scroll_panel(root, 69, 144, 328, 232);
    lv_obj_set_scrollbar_mode(viewport, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < 12; ++i)
        button(viewport, "列表", 0, i * 58, 328, 50, [](lv_event_t*) {}, 0x181d25);
    auto* first = lv_obj_get_child(viewport, 0);
    unsigned label_draws = 0;
    lv_obj_add_event_cb(lv_obj_get_child(first, 0), [](lv_event_t* event) {
        ++*static_cast<unsigned*>(lv_event_get_user_data(event));
    }, LV_EVENT_DRAW_MAIN, &label_draws);
    auto capture = [viewport](int offset) {
        lv_obj_scroll_to_y(viewport, offset, LV_ANIM_OFF);
        lv_obj_invalidate(viewport);
        lv_refr_now(nullptr);
        return framebuffer;
    };
    const auto live = capture(18);
    lv_obj_add_state(first, LV_STATE_PRESSED);
    const auto pressed = capture(18);
    lv_obj_remove_state(first, LV_STATE_PRESSED);
    cache_scroll_rows(viewport);
    advance(440);
    label_draws = 0;
    require(capture(18) == live && label_draws == 0,
            "A cached row must retain its pixels and skip live label drawing");
    lv_obj_add_state(first, LV_STATE_PRESSED);
    require(capture(18) == pressed && label_draws > 0,
            "Pressed cached rows must still draw their active state");
    lv_obj_clean(viewport);
    panel(viewport, 0, 0, 328, 464, 0x181d25, 18);
    const auto tick = lv_tick_get();
    input_history.push({true, 233, 340, tick});
    input_history.push({true, 233, 270, tick + 12});
    input_history.push({false, 233, 270, tick + 16});
    advance(180);
    require(lv_obj_get_scroll_y(viewport) > 0, "Lists with no button rows must still accept a swipe");
    lv_obj_scroll_to_y(viewport, 0, LV_ANIM_OFF);
    lv_obj_scroll_by(viewport, 0, 15, LV_ANIM_OFF);
    for (int i = 0; i <= 30; ++i) {
        input_history.push({true, 233, static_cast<int16_t>(340 - i), lv_tick_get()});
        advance(8);
    }
    require(lv_obj_get_scroll_y(viewport) >= 0, "Slow reverse movements must return from elastic overscroll");
    input_history.push({false, 233, 310, lv_tick_get()});
    advance(300);
    lv_obj_delete(viewport);
    viewport = scroll_panel(root, 70, 140, 326, 243, 0x152536, 20);
    auto* detail = text(viewport, "", 9, 12, 308, &toolbox_misans_20);
    std::string forecast;
    for (int i = 0; i < 14; ++i) forecast += "温度 22°   风力 3级\n";
    lv_label_set_text(detail, forecast.c_str());
    lv_obj_set_style_text_line_space(detail, 7, 0);
    lv_obj_scroll_to_y(viewport, 19, LV_ANIM_OFF);
    lv_refr_now(nullptr);
    const auto live_forecast = framebuffer;
    cache_scroll_rows(viewport);
    advance(80);
    lv_obj_invalidate(viewport);
    lv_refr_now(nullptr);
    require(framebuffer == live_forecast, "Cached text must preserve a rounded list's background and glyphs");
    flushed_pixels = 0;
    lv_obj_scroll_to_y(viewport, 27, LV_ANIM_OFF);
    lv_refr_now(nullptr);
    if (!full_render)
        require(flushed_pixels > 0 && flushed_pixels < 326 * 243 * 2 / 3,
                "Text scrolling should leave the fixed viewport margins untouched");
    const auto incremental_forecast = framebuffer;
    lv_obj_invalidate(viewport);
    lv_refr_now(nullptr);
    require(framebuffer == incremental_forecast, "Partial text redraw must match the full viewport");
    lv_obj_delete(root);
}

void check_drawer_persistence()
{
    auto app = [](int id, const char* name) {
        mooncake::AppProps_t props;
        props.appID = id;
        props.info.name = name;
        return props;
    };
    auto check_order = [](const view::LauncherView& drawer, std::initializer_list<const char*> names) {
        auto* scroll = lv_obj_get_child(drawer.root(), 2);
        const int first = lv_obj_get_scroll_x(scroll);
        int page = 0;
        for (const auto* name : names) {
            lv_obj_scroll_to_x(scroll, first + page * 466, LV_ANIM_OFF);
            lv_obj_update_layout(scroll);
            require(find_text(drawer.root(), name), "Drawer must restore every saved position");
            ++page;
        }
    };
    {
        view::LauncherView drawer;
        drawer.init({app(1, "Alpha"), app(2, "Beta"), app(3, "Gamma")});
        check_order(drawer, {"Alpha", "Beta", "Gamma"});
        require(drawer.remember_app(3), "Drawer must save the order after an app opens");
    }
    {
        view::LauncherView drawer;
        drawer.init({app(25, "Beta"), app(22, "Alpha"), app(33, "Delta"), app(18, "Gamma")});
        check_order(drawer, {"Gamma", "Alpha", "Beta", "Delta"});
        require(drawer.remember_app(25), "Drawer must save the next move-to-front");
    }
    {
        view::LauncherView drawer;
        drawer.init({app(18, "Gamma"), app(33, "Delta"), app(25, "Beta")});
        check_order(drawer, {"Beta", "Gamma", "Delta"});
    }
    Settings::storage.erase("toolboxdrawer_order");
}

void check_drawer(int count)
{
    std::vector<mooncake::AppProps_t> apps(count);
    const int first_id = 100 * count;
    for (int i = 0; i < count; ++i) {
        apps[i].appID = first_id + i;
        apps[i].info.name = "drawer app " + std::to_string(i);
        if (i % 2 == 0) apps[i].info.icon = const_cast<lv_image_dsc_t*>(&toolbox_app_stopwatch);
    }
    view::LauncherView view;
    view.init(std::move(apps));
    lv_obj_t* scroll = nullptr;
    for (uint32_t i = 0; i < lv_obj_get_child_count(view.root()); ++i) {
        auto* child = lv_obj_get_child(view.root(), i);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_dir(child) == LV_DIR_HOR)
            scroll = child;
    }
    require(scroll && lv_obj_get_child_count(scroll) == 6, "Drawer must have five reusable cards and one scroll bound");
    const int start = lv_obj_get_scroll_x(scroll);
    lv_obj_scroll_to_x(scroll, start - 466, LV_ANIM_OFF);
    lv_obj_update_layout(scroll);
    const auto last = "drawer app " + std::to_string(count - 1);
    require(find_text(view.root(), last.c_str()), "Going left from the first card must wrap");
    for (int page = 0; page < 80; ++page) {
        lv_obj_scroll_to_x(scroll, lv_obj_get_scroll_x(scroll) + 466, LV_ANIM_OFF);
        view.update();
        lv_obj_update_layout(scroll);
        const auto expected = "drawer app " + std::to_string(page % count);
        require(find_text(view.root(), expected.c_str()), "Rebase must preserve the selected app");
        require(lv_obj_get_child_count(scroll) == 6, "Card recycling must keep the object count bounded");
    }
    int clicked = -1;
    view.onAppClicked = [&](int id) { clicked = id; };
    const uint32_t at = lv_tick_get();
    input_history.push({true, 233, 232, at});
    input_history.push({false, 233, 232, at + 20});
    advance(40);
    view.update();
    require(clicked == first_id + 79 % count, "Reused card must open the selected app");

    const int before_swipe = lv_obj_get_scroll_x(scroll);
    for (int step = 0; step <= 8; ++step) {
        input_history.push({true, static_cast<int16_t>(380 - 36 * step), 232, lv_tick_get()});
        if (step <= 1) lv_indev_read(hardware.lvTouchpad);
        advance(12);
        if (step == 1)
            require(lv_obj_get_scroll_x(scroll) > before_swipe,
                    "A swipe must start in the blank margin before it reaches a card");
    }
    input_history.push({false, 92, 232, lv_tick_get()});
    advance(600);
    view.update();
    require(lv_obj_get_scroll_x(scroll) % 466 == 0 && find_text(view.root(), "drawer app 0"),
        "A horizontal swipe must settle on the next centered app");

    lv_refr_now(nullptr);
    const int centered = lv_obj_get_scroll_x(scroll);
    for (const int offset : {18, 225, 245, 440, 466}) {
        lv_obj_scroll_to_x(scroll, centered + offset, LV_ANIM_OFF);
        flushed_pixels = 0;
        lv_refr_now(nullptr);
        if (!full_render && offset == 18)
            require(flushed_pixels <= (170 + offset) * 170, "Drawer should redraw only the moving card");
        const auto incremental = framebuffer;
        lv_obj_invalidate(view.root());
        lv_refr_now(nullptr);
        require(incremental == framebuffer, "Partial drawer redraw must preserve card edges and indicators");
    }
}

void check_battery_monitor()
{
    toolbox::BatteryMonitor voltage_only;
    voltage_only.sample(4180, 0, false);
    for (int i = 0; i < 60; ++i) voltage_only.sample(4200, 0, false);
    require(voltage_only.sample(4200, 0, false).percent == 100,
            "Fractional voltage filtering must reach 4200 mV");

    toolbox::BatteryMonitor monitor;
    require(monitor.sample(4144, 5056, false).percent < 100,
            "A single idle reading during USB boot must not report full");
    for (int i = 0; i < 20; ++i) {
        const auto snapshot = monitor.sample(4192, 4968, true);
        require(snapshot.external_power && snapshot.charging && snapshot.percent < 100,
                "Active charging must stay below 100 percent");
    }
    for (int i = 0; i < 9; ++i) monitor.sample(4144, 5056, false);
    monitor.missed_sample();
    for (int i = 0; i < 9; ++i)
        require(monitor.sample(4144, 5056, false).percent < 100,
                "A read error must restart full-charge confirmation");
    const auto full = monitor.sample(4144, 5056, false);
    require(full.percent == 100 && full.external_power && !full.charging,
            "Stable idle at a near-full voltage must report 100 percent");
    require(monitor.sample(4120, 0, false).percent == 100,
            "A small voltage drop when unplugging must retain the full reading");
    for (int i = 0; i < 20; ++i) monitor.sample(3950, 0, false);
    require(monitor.sample(3950, 0, false).percent < 100,
            "The full reading must clear once the battery discharges");

    toolbox::BatteryMonitor termination;
    for (int i = 0; i < 3; ++i)
        require(termination.sample(4168, 4980, true).percent == 96,
                "A near-float charge phase must still report the estimated percentage");
    for (int i = 0; i < 9; ++i)
        require(termination.sample(4118, 5060, false).percent < 100,
                "Charge termination must be stable before reporting full");
    require(termination.sample(4118, 5060, false).percent == 100,
            "A normal voltage drop after termination must still count as full");
    const auto top_up = termination.sample(4170, 4980, true);
    require(top_up.percent == 100 && top_up.charging,
            "A top-up must retain the full reading and still show active charging");

    toolbox::BatteryMonitor unconfirmed;
    unconfirmed.sample(4168, 4980, true);
    for (int i = 0; i < 20; ++i)
        require(unconfirmed.sample(4118, 5060, false).percent < 100,
                "One near-float sample must not qualify an otherwise low idle voltage");

    toolbox::BatteryMonitor weak_input;
    toolbox::BatteryMonitor low_battery;
    for (int i = 0; i < 20; ++i) {
        require(weak_input.sample(4170, 4300, false).percent < 100, "Weak USB input must not count as charge completion");
        require(low_battery.sample(4090, 5000, false).percent < 100, "Idle at lower voltage must not count as charge completion");
    }
}

void check_lighting();

int render_all()
{
    check_battery_monitor();
    check_display_transfer();
    std::filesystem::create_directories(capture_dir);
    lv_init();
    auto* display = lv_display_create(468, 466);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(display, draw_buffer.data(), second_draw_buffer.data(), draw_buffer.size() * 2,
        full_render ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
        flushed_pixels += lv_area_get_size(area);
        if (lv_display_flush_is_last(display)) {
            for (size_t index = 0; index < framebuffer.size(); ++index) {
                const unsigned color = (pixels[index * 2] << 8) | pixels[index * 2 + 1];
                const unsigned red = (color >> 11) & 31, green = (color >> 5) & 63, blue = color & 31;
                framebuffer[index] = 0xff000000U | ((red * 255 / 31) << 16) |
                    ((green * 255 / 63) << 8) | (blue * 255 / 31);
            }
        }
        lv_display_flush_ready(display);
    });
    hardware.lvTouchpad = lv_indev_create();
    lv_indev_set_type(hardware.lvTouchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_scroll_limit(hardware.lvTouchpad, 6);
    lv_indev_set_read_cb(hardware.lvTouchpad, [](lv_indev_t* input, lv_indev_data_t* data) {
        static lv_indev_data_t last = {};
        toolbox::TouchSample sample;
        if (input_history.pop(sample)) {
            toolbox::route_touch_sample(input, data, sample, !input_history.empty());
            last = *data;
        } else {
            *data = last;
            data->continue_reading = false;
        }
    });
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    if (lighting_render) {
        check_lighting();
        return 0;
    }
    hardware.boot_logo = toolbox::ui::boot(lv_screen_active());
    screenshot("boot");
    toolbox::shell_init();
    view::LauncherView drawer;
    const char* names[] = {"秒表", "设置", "相机遥控", "计数器", "倒计时", "番茄钟", "水平仪", "天气"};
    const lv_image_dsc_t* icons[] = {&toolbox_app_stopwatch, &toolbox_app_settings, &toolbox_app_camera,
        &toolbox_app_counter, &toolbox_app_timers, &toolbox_app_focus, &toolbox_app_level, &toolbox_app_weather};
    std::vector<mooncake::AppProps_t> apps(std::size(names));
    for (size_t i = 0; i < apps.size(); ++i) {
        apps[i].appID = static_cast<int>(i);
        apps[i].info.name = names[i];
        apps[i].info.icon = const_cast<lv_image_dsc_t*>(icons[i]);
    }
    drawer.init(apps);
    toolbox::shell_attach(drawer.root());
    require(!hardware.boot_logo, "Home must release the startup screen on first attach");
    advance(40);
    screenshot("home");
    require(lv_obj_get_x(lv_obj_get_child(lv_screen_active(), -1)) == lv_obj_get_x(drawer.root()),
            "The lockscreen and the drawer must share the center of the visible circle");
    require_glyphs(lv_screen_active());
    for (int percent : {7, 100}) {
        hardware.battery_percent = percent;
        advance(1100);
        auto* label = find_text(lv_screen_active(), (std::to_string(percent) + "%").c_str());
        require(label != nullptr, "Missing dynamic battery percentage");
        auto* row = lv_obj_get_parent(label);
        lv_obj_update_layout(row);
        lv_area_t row_area, first_area, last_area;
        lv_obj_get_coords(row, &row_area);
        lv_obj_get_coords(lv_obj_get_child(row, 0), &first_area);
        lv_obj_get_coords(label, &last_area);
        require(std::abs(first_area.x1 + last_area.x2 - row_area.x1 - row_area.x2) <= 2,
                "Status row must stay centered as percentage width changes");
    }
    hardware.battery_percent = 82;
    advance(1100);
    auto battery_for_label = [](lv_obj_t* label) {
        require(label != nullptr, "Charging must retain the battery percentage");
        return lv_obj_get_child(lv_obj_get_parent(label), lv_obj_get_index(label) - 1);
    };
    auto check_power_bolt = [&](lv_obj_t* battery) {
        auto* bolt = lv_obj_get_child(battery, 3);
        require(lv_obj_check_type(bolt, &lv_image_class) && !lv_obj_has_flag(bolt, LV_OBJ_FLAG_HIDDEN),
                "External power must show the shared bolt image");
        lv_refr_now(display);
        lv_area_t mark, body;
        lv_obj_get_coords(bolt, &mark);
        lv_obj_get_coords(lv_obj_get_child(battery, 0), &body);
        require(mark.x2 < body.x1 && mark.y1 + mark.y2 == body.y1 + body.y2,
                "The bolt must sit to the left of the battery at the same vertical center");
        lv_area_t painted{mark.x2, mark.y2, mark.x1, mark.y1};
        for (int y = mark.y1; y <= mark.y2; ++y) {
            for (int x = mark.x1; x <= mark.x2; ++x) {
                const auto pixel = framebuffer[y * 468 + x];
                const auto green = (pixel >> 8) & 255;
                if (green <= ((pixel >> 16) & 255) + 35 || green <= (pixel & 255) + 15) continue;
                painted.x1 = std::min(painted.x1, x);
                painted.x2 = std::max(painted.x2, x);
                painted.y1 = std::min(painted.y1, y);
                painted.y2 = std::max(painted.y2, y);
            }
        }
        require(lv_area_get_width(&painted) >= 12 && lv_area_get_height(&painted) >= 16,
                "The rendered mint bolt must have a visible footprint of at least 12x16 pixels");
    };
    auto* status_text = find_text(lv_screen_active(), "82%");
    auto* digital_battery = battery_for_label(status_text);
    const int percent_x = lv_obj_get_x(status_text);
    hardware.external_power = hardware.charging = true;
    advance(1100);
    check_power_bolt(digital_battery);
    require(lv_obj_get_x(status_text) == percent_x && std::strcmp(lv_label_get_text(status_text), "82%") == 0,
            "The percentage must stay in place when charging starts");
    screenshot("home-charging");
    hardware.charging = false;
    hardware.battery_percent = 100;
    advance(1100);
    require(std::strcmp(lv_label_get_text(status_text), "100%") == 0, "The plugged-in face must show the full percentage");
    check_power_bolt(digital_battery);
    hardware.external_power = false;
    advance(1100);
    require(lv_obj_has_flag(lv_obj_get_child(digital_battery, 3), LV_OBJ_FLAG_HIDDEN),
            "Lightning must hide when external power is disconnected");

    hardware.battery_percent = 85;
    advance(1100);
    const auto displayed_face = framebuffer;
    auto check_live_handoff = [&] {
        lv_refr_now(display);
        const auto incremental = framebuffer;
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        require(framebuffer == incremental, "Incremental handoff must match a complete live-page redraw");
    };
    toolbox::shell_touch(true, 233, 320);
    advance(16);
    toolbox::shell_touch(true, 233, 230);
    lv_refr_now(display);
    const auto revealed = framebuffer;
    constexpr int edge = 466 - (320 - 230);
    constexpr int center = 233;
    constexpr uint32_t boundary_color = 0xff526983;
    require(revealed[edge * 468 + center] == boundary_color,
            "The wipe edge must move up by exactly the finger's 90 pixels");
    require(std::equal(displayed_face.begin(), displayed_face.begin() + edge * 468,
                       revealed.begin()), "Rows above the edge must retain the visible face");
    toolbox::shell_touch(true, 233, 221);
    flushed_pixels = 0;
    lv_refr_now(display);
    require(framebuffer[(edge - 9) * 468 + center] == boundary_color,
            "An additional nine-pixel drag must move the edge nine pixels");
    if (!full_render) require(flushed_pixels > 0 && flushed_pixels <= 468 * 10,
                              "Nine pixels of finger travel should flush only the swept band");
    toolbox::shell_touch(true, 233, 230);
    lv_refr_now(display);
    require(framebuffer == revealed, "Reversing to the same position must restore every pixel");
    toolbox::shell_cancel_touch();
    advance(300);
    check_live_handoff();
    const auto face_children = lv_obj_get_child_count(lv_screen_active());
    input_history.push({true, 233, 320, lv_tick_get()});
    advance(16);
    input_history.push({true, 233, 250, lv_tick_get()});
    advance(40);
    require(lv_obj_get_child_count(lv_screen_active()) == face_children + 1, "Touch must enter the wipe");
    input_history.push({true, 233, 320, lv_tick_get()});
    input_history.push({false, 233, 320, lv_tick_get() + 4});
    advance(40);
    require(toolbox::shell_is_locked() && lv_obj_get_child_count(lv_screen_active()) == face_children,
            "A buffered reversal followed by release must restore the full watch face");
    hardware.battery_percent = 82;
    advance(1100);
    // Entire quick gesture captured while GUI was busy, then replayed at once.
    uint32_t captured_at = lv_tick_get();
    input_history.push({true, 233, 300, captured_at});
    for (int time = 4; time <= 200; time += 4)
        input_history.push({true, 233, 300, captured_at + static_cast<uint32_t>(time)});
    input_history.push({true, 233, 280, captured_at + 216});
    input_history.push({false, 233, 280, captured_at + 220});
    advance(280); advance(300);
    require(!toolbox::shell_is_locked(), "Buffered short flick must use sample timestamps");
    toolbox::shell_lock();
    toolbox::shell_key_unlock();
    advance(80);
    require(toolbox::shell_is_locked(), "Key unlock should animate");
    toolbox::shell_key_unlock();
    advance(220);
    require(!toolbox::shell_is_locked(), "Repeated key should not restart unlock");
    toolbox::shell_lock();
    const unsigned screen_children = lv_obj_get_child_count(lv_screen_active());
    toolbox::shell_touch(true, 233, 300);
    advance(20);
    toolbox::shell_touch(true, 233, 260);
    require(lv_obj_get_child_count(lv_screen_active()) == screen_children + 1,
            "Unlock should use one wipe surface");
    screenshot("unlock-drag");
    advance(150);
    toolbox::shell_touch(false, 233, 260);
    advance(300);
    require(toolbox::shell_is_locked() && lv_obj_get_child_count(lv_screen_active()) == screen_children,
            "Canceled unlock must release cached page images");
    swipe(233, 300, 240, 600); advance(300);
    require(toolbox::shell_is_locked(), "Short drag must rebound");
    swipe(233, 300, 260, 60); advance(300);
    require(!toolbox::shell_is_locked(), "Short quick flick must continue with inertia");
    toolbox::shell_lock();
    toolbox::shell_touch(true, 233, 300);
    advance(200); // A held finger can still finish with a short flick.
    toolbox::shell_touch(true, 233, 300);
    advance(16);
    toolbox::shell_touch(true, 233, 280);
    for (int sample = 0; sample < 5; ++sample) {
        advance(8);
        toolbox::shell_touch(true, 233, 280); // Repeated controller coordinates.
    }
    toolbox::shell_touch(false, 233, 280);
    advance(300);
    require(!toolbox::shell_is_locked(), "20px flick must survive repeated touch samples");
    toolbox::shell_lock();
    swipe(233, 300, 230, 600); advance(300);
    require(!toolbox::shell_is_locked(), "70px slow drag must unlock");
    toolbox::shell_lock();
    toolbox::shell_touch(true, 233, 300);
    advance(40); toolbox::shell_touch(true, 233, 220);
    advance(16); toolbox::shell_touch(true, 233, 260);
    toolbox::shell_touch(false, 233, 260); advance(300);
    require(toolbox::shell_is_locked(), "Downward reversal must cancel unlock");
    toolbox::shell_lock();
    swipe(233, 300, 140); advance(250);
    require(!toolbox::shell_is_locked(), "Long drag must unlock");
    check_live_handoff();
    drawer.update(); screenshot("drawer");
    require(std::equal(framebuffer.begin() + (edge + 1) * 468, framebuffer.end(),
                       revealed.begin() + (edge + 1) * 468),
            "Revealed rows must match the live drawer");
    toolbox::shell_lock();
    swipe(233, 20, 100); advance(220); screenshot("clock");
    const auto clock_frame = framebuffer;
    auto* overlay = lv_obj_get_child(lv_layer_top(), 1);
    advance(1200);
    require(!lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Clock disappeared early");
    advance(310);
    require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Clock must close after 1.5 seconds");
    swipe(233, 20, 100); advance(220);
    swipe(233, 60, 140); advance(220); screenshot("control");
    const auto control_frame = framebuffer;
    swipe(233, 380, 280); advance(220);
    require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Control upward swipe must close directly");
    advance(1600);
    require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Clock must stay hidden after control dismissal");
    // Pull-down sheets repaint only what moved. Every step of a drag and of the
    // settle animation must still match a complete redraw.
    auto drag = [&](int x, int from, int to, int steps, int interval) {
        toolbox::shell_touch(true, x, from);
        for (int step = 1; step <= steps; ++step) {
            advance(interval);
            toolbox::shell_touch(true, x, from + (to - from) * step / steps);
            check_live_handoff();
        }
    };
    auto settle = [&] {
        for (int time = 0; time < 260; time += 10) {
            advance(10);
            check_live_handoff();
        }
    };
    auto* clock_card = lv_obj_get_child(overlay, 0);
    auto* control_center = lv_obj_get_child(overlay, 1);
    lv_refr_now(display);
    toolbox::shell_touch(true, 233, 20);
    advance(100);
    flushed_pixels = 0;
    toolbox::shell_touch(true, 233, 40);
    lv_refr_now(display);
    if (!full_render) require(flushed_pixels > 0 && flushed_pixels <= 240 * 140,
                              "Starting a pull must repaint the time card only");
    toolbox::shell_touch(false, 233, 40);
    settle();
    require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "A short slow pull must retreat");
    drag(233, 20, 110, 9, 10);
    toolbox::shell_touch(false, 233, 110);
    settle();
    require(!lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN) && framebuffer == clock_frame,
            "A dragged pull must settle on the same time card as a swipe");
    drag(233, 30, 100, 7, 40);
    toolbox::shell_touch(false, 233, 100);
    settle();
    require(lv_obj_has_flag(control_center, LV_OBJ_FLAG_HIDDEN) && !lv_obj_has_flag(clock_card, LV_OBJ_FLAG_HIDDEN) &&
            framebuffer == clock_frame, "An abandoned second pull must leave the time card showing");
    drag(233, 30, 210, 9, 10);
    flushed_pixels = 0;
    toolbox::shell_touch(true, 233, 226);
    lv_refr_now(display);
    if (!full_render) require(flushed_pixels > 0 && flushed_pixels < 468 * 466 / 2,
                              "Dragging the control center must repaint its content, not the whole sheet");
    check_live_handoff();
    drag(233, 226, 390, 8, 10);
    toolbox::shell_touch(false, 233, 390);
    settle();
    require(!lv_obj_has_flag(control_center, LV_OBJ_FLAG_HIDDEN) && framebuffer == control_frame,
            "A dragged second pull must settle on the same control center as a swipe");
    drag(120, 400, 60, 17, 10);
    toolbox::shell_touch(false, 120, 60);
    settle();
    require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Dragging the control center up must close it");
    advance(1600);
    require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Clock must stay hidden after a dragged dismissal");
    toolbox::shell_open_settings(); menu_clearance(); screenshot("settings");
    auto* settings_page = lv_obj_get_child(lv_layer_top(), 0);
    auto* settings_list = lv_obj_get_child(settings_page, 2);
    require_menu_row(settings_page, "Wi-Fi");
    lv_obj_scroll_to_y(settings_list, lv_obj_get_scroll_bottom(settings_list), LV_ANIM_OFF);
    screenshot("settings-more");
    lv_obj_scroll_to_y(settings_list, 0, LV_ANIM_OFF);
    captured_at = lv_tick_get();
    input_history.push({true, 233, 180, captured_at});
    input_history.push({true, 233, 160, captured_at + 8});
    input_history.push({true, 233, 140, captured_at + 16});
    input_history.push({true, 233, 120, captured_at + 24});
    input_history.push({false, 233, 120, captured_at + 28});
    advance(150);
    require(lv_obj_is_valid(settings_list) && find_text(settings_page, "Bluetooth") &&
            lv_obj_get_scroll_y(settings_list) > 0, "Buffered fast list swipe must scroll without clicking Wi-Fi");
    toolbox::shell_close_settings();
    toolbox::shell_open_settings(); advance(40);
    captured_at = lv_tick_get();
    input_history.push({true, 233, 180, captured_at});
    for (int sample = 1; sample < 80; ++sample)
        input_history.push({true, 233, static_cast<int16_t>(180 + sample % 20), captured_at + sample * 4});
    require(input_history.size() <= 4 && !input_history.overflows(), "Moves must coalesce without losing gesture edges");
    input_history.cancel(captured_at + 316);
    input_history.push({false, 233, 180, captured_at + 320});
    advance(400);
    require(find_text(settings_page, "Bluetooth") != nullptr, "Canceled input must not click a menu row");
    captured_at = lv_tick_get();
    input_history.push({true, 233, 180, captured_at});
    input_history.push({false, 233, 180, captured_at + 40});
    advance(80);
    require(find_text(settings_page, "重新配网") != nullptr, "Normal tap must work after cancellation");
    menu_clearance(); screenshot("wifi");
    click_text("重新配网"); advance(1100); screenshot("ap");
    hardware.network.configMode = false; advance(1100);
    toolbox::shell_close_settings();
    toolbox::shell_open_settings(); click_text("表盘样式"); menu_clearance(); screenshot("faces");
    click_text("指针表盘"); advance(40);
    toolbox::shell_close_settings();
    advance(1100); // Let the hands and the date appear.
    screenshot("analog");
    auto* weekday = find_text(lv_screen_active(), "周一");
    require(weekday != nullptr, "Analog face must show the Chinese weekday");
    require_glyphs(lv_screen_active());
    hardware.external_power = true;
    hardware.battery_percent = 100;
    advance(1100);
    check_power_bolt(battery_for_label(find_text(lv_obj_get_parent(weekday), "100%")));
    screenshot("analog-charging");
    drag(233, 20, 110, 9, 10);
    toolbox::shell_touch(false, 233, 110);
    settle();
    check_power_bolt(battery_for_label(find_text(clock_card, "100%")));
    screenshot("clock-charging");
    drag(233, 30, 390, 18, 10);
    toolbox::shell_touch(false, 233, 390);
    settle();
    require(!lv_obj_has_flag(control_center, LV_OBJ_FLAG_HIDDEN), "Analog face must open the control center");
    check_power_bolt(battery_for_label(find_text(control_center, "100%")));
    screenshot("control-charging");
    const auto analog_controls_frame = framebuffer;
    drag(120, 400, 300, 5, 10);
    toolbox::shell_cancel_touch();
    settle();
    require(framebuffer == analog_controls_frame, "Canceling the cached close must restore the live controls");
    drag(120, 400, 60, 17, 10);
    toolbox::shell_touch(false, 120, 60);
    settle();
    require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Closing controls must restore the analog face");
    hardware.external_power = false;
    hardware.battery_percent = 82;
    toolbox::shell_detach();
    {
        toolbox::StopwatchPage stopwatch([](){}, [](){}, [](){});
        stopwatch.update(0, model::Stopwatch::State_t::Stopped, {}); screenshot("stopwatch");
    }
    {
        auto* battery = toolbox::ui::battery(lv_screen_active());
        for (int value : {0, 25, 50, 82, 100}) {
            toolbox::ui::battery_level(battery, value);
            lv_obj_update_layout(battery);
            require(value == 0 ? lv_obj_has_flag(lv_obj_get_child(battery, 2), LV_OBJ_FLAG_HIDDEN)
                               : lv_obj_get_width(lv_obj_get_child(battery, 2)) == 14 * value / 100,
                    "Battery solid fill must follow actual percentage");
        }
        lv_obj_delete(battery);
        AppStopWatch app;
        app.baseCreate(); app.open(); app.baseUpdate();
        auto poll = [&](bool a, bool b, int milliseconds) {
            hardware.rawA = a; hardware.rawB = b;
            for (int time = 0; time < milliseconds; time += 10) { advance(10); app.baseUpdate(); }
        };
        poll(false, false, 40);
        poll(false, true, 40); poll(false, false, 200);
        require(find_text(lv_screen_active(), "暂停") != nullptr, "B must start stopwatch on press");
        poll(true, false, 40); poll(false, false, 100);
        require(find_text(lv_screen_active(), "记录 1") != nullptr, "A must record lap while running");
        auto* records = find_text(lv_screen_active(), "记录 1");
        lv_obj_send_event(lv_obj_get_parent(records), LV_EVENT_CLICKED, nullptr);
        app.baseUpdate();
        require(find_text(lv_screen_active(), "第1圈") != nullptr, "Recorded laps must be visible");
        require_menu_row(lv_screen_active(), "第1圈");
        auto* background_button = lv_obj_get_parent(find_text(lv_screen_active(), "暂停"));
        lv_area_t bounds;
        lv_obj_get_coords(background_button, &bounds);
        lv_point_t button_center{(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
        require(lv_indev_search_obj(lv_screen_active(), &button_center) != background_button,
                "Touches on the lap overlay must not hit the stopwatch buttons underneath");
        screenshot("stopwatch-records");
        poll(false, true, 40); poll(false, false, 100);
        require(find_text(lv_screen_active(), "复位") != nullptr, "B must pause stopwatch");
        poll(false, true, 40); poll(false, false, 100);
        require(find_text(lv_screen_active(), "暂停") != nullptr, "B must resume stopwatch");
        poll(false, true, 40); poll(false, false, 100);
        poll(true, false, 40); poll(false, false, 100);
        require(find_text(lv_screen_active(), "记录 0") != nullptr && find_text(lv_screen_active(), "00:00") != nullptr,
                "A must reset elapsed time and records while paused");
        poll(true, true, 650);
        require(app.currentState() == mooncake::AppAbility::StateSleeping, "A+B hold must return home");
    }
    {
        AppCamera app;
        hardware.rawA = hardware.rawB = false;
        app.baseCreate(); app.open(); app.baseUpdate();
        auto poll = [&](bool a, bool b, int milliseconds) {
            hardware.rawA = a; hardware.rawB = b;
            for (int time = 0; time < milliseconds; time += 10) { advance(10); app.baseUpdate(); }
        };
        auto camera_click = [&](const char* text) {
            auto* caption = find_text(lv_obj_get_child(lv_screen_active(), -1), text);
            require(caption != nullptr, text);
            auto* target = lv_obj_get_parent(caption);
            while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
            require(target != nullptr, "Missing camera action");
            lv_obj_send_event(target, LV_EVENT_CLICKED, nullptr);
            poll(false, false, 50);
        };
        auto keyA = [&] { poll(true, false, 80); poll(false, false, 100); };
        auto keyB = [&] { poll(false, true, 80); poll(false, false, 100); };
        auto wheel_tap = [&](int16_t y) {
            input_history.push({true, 233, y, lv_tick_get()});
            input_history.push({false, 233, y, lv_tick_get() + 30});
            poll(false, false, 60);
        };
        auto wheel_drag = [&](int16_t from, int16_t to, unsigned wait) {
            input_history.push({true, 233, from, lv_tick_get()});
            input_history.push({true, 233, int16_t((from + to) / 2), lv_tick_get() + 16});
            input_history.push({true, 233, to, lv_tick_get() + 32});
            input_history.push({false, 233, to, lv_tick_get() + 48});
            poll(false, false, wait);
        };
        auto wheel_position = [](lv_obj_t* label) {
            lv_area_t bounds;
            lv_obj_get_coords(label, &bounds);
            return (bounds.y1 + bounds.y2) / 2;
        };
        screenshot("sony-v4-connect");
        camera_click("反馈设置"); screenshot("sony-v4-feedback");
        camera_click("操作音效");
        require(find_text(lv_screen_active(), "关闭"), "Feedback switch must update");
        keyA();
        keyB();
        require(find_text(lv_screen_active(), "Wi-Fi 配网"), "KEYB must choose Wi-Fi");
        screenshot("sony-v4-pair");
        camera_click("手机辅助"); screenshot("sony-v4-phone"); screenshot("sony-v4-stage4-phone");
        require(find_text(lv_screen_active(), "ToolBox-Camera-TEST"), "Phone must display the current portal");
        require(find_text(lv_screen_active(), "手机连接此热点"), "Phone setup must show the open hotspot");
        sony::ConnectionState phone_pin;
        phone_pin.phase = sony::ConnectionPhase::Verify;
        phone_pin.fingerprint = "SHA256:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopq";
        phone_pin.fingerprintChanged = true;
        sony::preview::report(phone_pin); poll(false, false, 50);
        require(find_text(lv_screen_active(), "指纹已变化，请与相机核对"), "A changed host key requires review");
        screenshot("sony-v4-changed-pin");
        keyA();
        require(sony::Connection::instance().state().phase == sony::ConnectionPhase::Idle && !sony::preview::confirmed,
                "Cancelling a changed fingerprint must close the pending connection");
        keyB(); screenshot("sony-v4-scan");
        keyB(); screenshot("sony-v4-key-lower");
        camera_click("ABC"); screenshot("sony-v4-key-upper");
        camera_click("符号"); screenshot("sony-v4-key-symbol");
        camera_click("123"); screenshot("sony-v4-key-number");
        for (const char* digit : {"1","2","3","4","5","6","7","8"}) camera_click(digit);
        require(find_text(lv_screen_active(), "12345678"), "Wi-Fi password is readable on the keyboard");
        keyB();
        require(find_text(lv_screen_active(), "SSH 用户名"), "KEYB confirms Wi-Fi password");
        keyA();
        require(find_text(lv_screen_active(), "12345678"), "KEYA returns to previous credential");
        keyB();
        camera_click("jkl");
        keyB();
        camera_click("pqrs");
        keyB();
        require(sony::preview::submitted.valid() && sony::preview::submitted.user == "j", "Keypad must send completed fields");
        screenshot("sony-v4-connecting");
        sony::ConnectionState verify;
        verify.phase = sony::ConnectionPhase::Verify;
        verify.fingerprint = "SHA256:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopq";
        sony::preview::report(verify); poll(false, false, 50);
        screenshot("sony-v4-fingerprint");
        require(!sony::preview::confirmed, "Fingerprint confirmation requires explicit approval");
        keyB();
        require(sony::preview::confirmed, "KEYB confirms displayed fingerprint");
        sony::ConnectionState ready;
        ready.phase = sony::ConnectionPhase::Ready;
        ready.model = "ILCE-7M5";
        ready.capture.canPhoto = ready.capture.canMovie = true;
        ready.capture.recording = sony::Recording::Idle;
        using sony::CaptureAction;
        using sony::Parameter;
        auto params = std::make_shared<sony::Parameters>();
        auto setting = [&](Parameter id, unsigned type, int64_t value, std::vector<int64_t> allowed = {}) {
            sony::CameraParameter p;
            p.id = id; p.type = uint16_t(type); p.current = value; p.allowed = std::move(allowed);
            params->push_back(std::move(p));
        };
        setting(Parameter::Shutter, 6, 0x100fa, {0x1007d, 0x100fa, 0x101f4});
        setting(Parameter::Aperture, 4, 280, {200, 280, 400});
        setting(Parameter::Iso, 6, 400, {100, 200, 400, 800, 1600, 3200, 6400});
        setting(Parameter::Ev, 3, -300, {-300, 0, 300});
        setting(Parameter::WhiteBalance, 4, 0x8012, {2, 4, 0x8001, 0x8012, 0x8020, 0x8030});
        setting(Parameter::Focus, 4, 0x8004);
        setting(Parameter::Drive, 6, 1);
        setting(Parameter::PhotoFormat, 2, 2);
        setting(Parameter::RawCompression, 2, 7, {6, 7, 1});
        setting(Parameter::MovieFormat, 2, 0x08);
        setting(Parameter::MovieRate, 2, 6);
        setting(Parameter::MovieSetting, 4, 0x36);
        setting(Parameter::Mode, 6, 1);
        setting(Parameter::Battery, 1, 78);
        ready.capture.parameters = params;
        sony::preview::report(ready); poll(false, false, 50);
        screenshot("sony-v4-stage4-console");
        require(find_text(lv_screen_active(), "Wi-Fi 已连接") && find_text(lv_screen_active(), "ILCE-7M5") &&
                !find_text(lv_screen_active(), "α7 V"), "Console shows the camera's model verbatim");
        require(!find_text(lv_screen_active(), "机身") && !find_text(lv_screen_active(), "相册"),
                "Later stages stay hidden");
        sony::preview::commands.clear();
        camera_click("设置");
        camera_click("操作音效");
        keyA();
        const auto savedPower = hardware.getBatteryStatus();
        hardware.external_power = hardware.charging = false;
        camera_click("拍照");
        auto* localPercent = find_text(lv_screen_active(), "本机 82%");
        require(localPercent, "Camera page shows the watch battery separately");
        auto* statusRow = lv_obj_get_parent(localPercent);
        auto* bolt = lv_obj_get_child(lv_obj_get_child(statusRow, 1), 3);
        require(lv_obj_has_flag(bolt, LV_OBJ_FLAG_HIDDEN), "Unplugged watch hides charge bolt");
        hardware.external_power = hardware.charging = true;
        poll(false, false, 1100);
        require(!lv_obj_has_flag(bolt, LV_OBJ_FLAG_HIDDEN), "Watch charge bolt updates in place");
        screenshot("sony-v4-stage4-photo");
        hardware.external_power = hardware.charging = false;
        poll(false, false, 1100);
        require(lv_obj_has_flag(bolt, LV_OBJ_FLAG_HIDDEN), "Unplugging hides the charge bolt again");
        hardware.external_power = savedPower.external_power;
        hardware.charging = savedPower.charging;
        require(find_text(lv_screen_active(), preview_analog ? "10:09" : "18:36") &&
                find_text(lv_screen_active(), "相机 78%"),
                "Capture page must show the local clock and camera battery");
        require(sony::preview::commands.empty(), "Connecting and entering a capture page performs no writes");
        camera_click("ISO"); screenshot("sony-v4-stage4-value");
        auto* oldValue = find_text(lv_screen_active(), "800");
        require(oldValue && wheel_position(oldValue) > 290, "Next ISO starts below the selection");
        wheel_drag(258, 236, 80);
        require(wheel_position(oldValue) < 290 && wheel_position(oldValue) > 253, "ISO slides during the transition");
        poll(false, false, 150);
        auto* last = find_text(lv_screen_active(), "800");
        require(last && wheel_position(last) >= 250 && wheel_position(last) < 264, "Upward swipe advances the ISO value");
        require_glyphs(lv_obj_get_child(lv_screen_active(), -1));
        keyA();
        require(find_text(lv_screen_active(), "KEYB 拍照"), "Cancelling a quick edit returns to the photo screen");
        require(sony::preview::commands.empty(), "Swiping then cancelling performs no write");
        camera_click("ISO");
        wheel_drag(306, 200, 230);
        auto* highIso = find_text(lv_screen_active(), "3200");
        require(highIso && wheel_position(highIso) >= 250 && wheel_position(highIso) < 264,
                "A long upward drag crosses three allowed values");
        wheel_drag(190, 296, 230);
        auto* originalIso = find_text(lv_screen_active(), "400");
        require(originalIso && wheel_position(originalIso) >= 250 && wheel_position(originalIso) < 264,
                "A long downward drag reverses all three steps");
        keyA();
        camera_click("参数"); screenshot("sony-v4-stage4-photo-parameters");
        require_menu_row(lv_screen_active(), "快门");
        camera_click("白平衡");
        require(find_text(lv_screen_active(), "相机当前 色温") && find_text(lv_screen_active(), "荧光灯 暖白色") &&
                find_text(lv_screen_active(), "自定义 1"), "White balance uses Sony wire labels");
        require_glyphs(lv_obj_get_child(lv_screen_active(), -1));
        keyA();
        camera_click("RAW 压缩");
        require(find_text(lv_screen_active(), "相机当前 已压缩 (HQ)") && find_text(lv_screen_active(), "无损压缩"),
                "The advertised RAW compression code has a label");
        require_glyphs(lv_obj_get_child(lv_screen_active(), -1));
        keyA();
        camera_click("ISO");
        wheel_tap(190);
        poll(false, false, 160);
        require_glyphs(lv_obj_get_child(lv_screen_active(), -1));
        keyA();
        require(find_text(lv_screen_active(), "拍照参数"), "Cancelling a list edit returns to the parameter list");
        auto* firstRow = find_text(lv_screen_active(), "快门");
        const auto firstRowPosition = wheel_position(firstRow);
        input_history.push({true, 233, 327, lv_tick_get()});
        input_history.push({true, 233, 272, lv_tick_get() + 16});
        input_history.push({true, 233, 214, lv_tick_get() + 32});
        input_history.push({false, 233, 214, lv_tick_get() + 48});
        poll(false, false, 160);
        require(find_text(lv_screen_active(), "快门") == firstRow && wheel_position(firstRow) < firstRowPosition - 30,
                "Camera list scroll reuses its rows");
        auto* list = lv_obj_get_parent(lv_obj_get_parent(firstRow));
        lv_obj_scroll_to_y(list, lv_obj_get_scroll_y(list) + lv_obj_get_scroll_bottom(list), LV_ANIM_OFF);
        screenshot("sony-photo-parameters-more");
        keyA();
        camera_click("ISO");
        wheel_tap(320);
        poll(false, false, 160);
        camera_click("应用");
        require(sony::preview::commands.size() == 1, "Explicit apply sends one request");
        require(sony::preview::commands.back().value == 800, "Explicit apply sends the selected value");
        require(find_text(lv_screen_active(), "KEYB 拍照") && find_text(lv_screen_active(), "400"),
                "Explicit apply returns immediately and leaves the last confirmed value visible");
        params = std::make_shared<sony::Parameters>(*params);
        (*params)[2].current = 800;
        ready.capture.parameters = params;
        ready.capture.resultId = 1;
        sony::preview::report(ready); poll(false, false, 50);
        require(find_text(lv_screen_active(), "800") && find_text(lv_screen_active(), "KEYB 拍照"),
                "Actual readback updates the exposure after return");
        sony::preview::stateReads = 0;
        poll(false, false, 1500);
        require(sony::preview::stateReads == 0, "An idle capture page does not copy the shared camera state");
        sony::preview::report(ready); poll(false, false, 50);
        require(sony::preview::stateReads == 1, "One published revision is copied exactly once");
        keyB();
        require(sony::preview::commands.back().action == CaptureAction::Photo, "KEYB sends one photo command");
        ready.capture.resultId = 2;
        ready.capture.result = 0x2019;
        sony::preview::report(ready); poll(false, false, 50);
        require(find_text(lv_screen_active(), "相机忙，请稍候"), "A busy camera cannot claim capture success");
        camera_click("录像"); screenshot("sony-v4-stage4-movie");
        camera_click("参数"); screenshot("sony-v4-stage4-movie-parameters");
        require(!find_text(lv_screen_active(), "驱动模式") && !find_text(lv_screen_active(), "照片格式"),
                "Movie list must exclude still-only parameters");
        keyA();
        const auto audioBefore = hardware.tone_count;
        const auto vibrationBefore = hardware.vibration_count;
        keyB();
        require(sony::preview::commands.back().action == CaptureAction::Start && hardware.camera_muted &&
                toolbox::touch_input_blocked() && !find_text(lv_screen_active(), "REC"),
                "Start locks immediately, REC waits for camera");
        auto active = ready;
        active.capture.recording = sony::Recording::Active;
        active.capture.since = lv_tick_get() - 123500;
        active.capture.localStart = true;
        active.capture.resultId = 3;
        active.capture.result = 0;
        sony::preview::report(active); poll(false, false, 50);
        screenshot("sony-v4-stage4-recording");
        auto* timerLabel = find_text(lv_screen_active(), "00:02:03");
        require(timerLabel, "Recording uses the send-time plus 500 ms baseline");
        keyA();
        poll(true, true, 950); poll(false, true, 50); poll(false, false, 100);
        input_history.push({true, 233, 20, lv_tick_get()});
        input_history.push({true, 233, 130, lv_tick_get() + 20});
        input_history.push({false, 233, 130, lv_tick_get() + 40});
        poll(false, false, 90);
        require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN) && find_text(lv_screen_active(), "REC") &&
                sony::preview::commands.back().action == CaptureAction::Start &&
                find_text(lv_screen_active(), "00:02:04") == timerLabel, "Touch, KEYA and chord stay locked and the timer reuses its label");
        keyB();
        require(sony::preview::commands.back().action == CaptureAction::Stop &&
                find_text(lv_screen_active(), "REC") && hardware.camera_muted, "Stop waits for camera confirmation");
        require(hardware.tone_count == audioBefore && hardware.vibration_count == vibrationBefore + 2,
                "Start and stop vibrate once each without sound");
        auto lost = ready;
        lost.phase = sony::ConnectionPhase::Connecting;
        lost.capture = {};
        lost.capture.uncertain = true;
        sony::preview::report(lost); poll(false, false, 50);
        require(hardware.camera_muted && toolbox::touch_input_blocked() && find_text(lv_screen_active(), "录像状态未知"),
                "Disconnect must preserve the lock and show unknown");
        const auto afterStop = sony::preview::commands.size();
        active.capture.localStart = false;
        active.capture.since = lv_tick_get();
        sony::preview::report(active); poll(false, false, 50);
        require(find_text(lv_screen_active(), "从检测到录像时计时") && sony::preview::commands.size() == afterStop,
                "Reconnect adopts camera state without issuing another movie command");
        ready.capture.result = 0;
        ready.capture.resultId = 4;
        sony::preview::report(ready); poll(false, false, 50);
        require(!hardware.camera_muted && !toolbox::touch_input_blocked() &&
                find_text(lv_screen_active(), "KEYB 开始录像"), "Confirmed idle restores movie ready");
        keyA(); keyA();
        sony::ConnectionState retry;
        retry.phase = sony::ConnectionPhase::Connecting;
        retry.message = "正在重连相机";
        sony::preview::report(retry); poll(false, false, 50);
        require(find_text(lv_screen_active(), "正在重连相机"), "A dropped session must show reconnection state");
        sony::preview::report(ready); poll(false, false, 50);
        require(find_text(lv_screen_active(), "Wi-Fi 已连接"), "Reconnect returns to the real model");
        {
            // Stage 5 body controls against the desktop camera mock.
            using sony::BodyCommand;
            auto body = ready;
            body.body.buttons = {1, 2, 3, 4, 5, 6, 0x10, 0x11, 0x14, 0x15, 0x16, 0x1c};
            body.body.dials = {0x4001, 0x4002, 0x4003};
            sony::preview::report(body); poll(false, false, 50);
            require(find_text(lv_screen_active(), "机身"), "Console offers body controls the camera lists");
            screenshot("sony-v4-stage5-console");
            auto& sent = sony::preview::bodyCommands;
            sent.clear();
            const auto captureBefore = sony::preview::commands.size();
            camera_click("机身");
            require(find_text(lv_screen_active(), "机身控制") && find_text(lv_screen_active(), "MENU") &&
                    find_text(lv_screen_active(), "Fn") && find_text(lv_screen_active(), "SET") &&
                    find_text(lv_screen_active(), "回放") && find_text(lv_screen_active(), "拨轮"),
                    "Remote page shows the listed body controls");
            constexpr int cx = 234, cy = 260; // wheel centre on the 468-px frame
            auto point = [&](int degrees, int radius) {
                const double r = degrees * 3.14159265358979 / 180;
                return std::pair<int16_t, int16_t>{int16_t(cx + std::lround(radius * std::cos(r))),
                                                   int16_t(cy + std::lround(radius * std::sin(r)))};
            };
            auto wheel_turn = [&](int from, int to) {
                auto t = lv_tick_get();
                const int stride = to > from ? 5 : -5;
                int a = from, samples = 1;
                auto p = point(a, 80);
                input_history.push({true, p.first, p.second, t});
                while (a != to) {
                    a = std::abs(to - a) < 5 ? to : a + stride;
                    p = point(a, 80);
                    input_history.push({true, p.first, p.second, t += 16});
                    ++samples;
                }
                input_history.push({false, p.first, p.second, t += 16});
                poll(false, false, unsigned(samples * 16 + 80));
            };
            auto tap_at = [&](int16_t x, int16_t y) {
                input_history.push({true, x, y, lv_tick_get()});
                input_history.push({false, x, y, lv_tick_get() + 40});
                poll(false, false, 90);
            };
            auto dial_steps = [&](uint16_t id) {
                int steps = 0;
                for (const auto& c : sent) if (c.kind == BodyCommand::Dial && c.id == id) steps += c.steps;
                return steps;
            };
            auto buttons_sent = [&] {
                return std::count_if(sent.begin(), sent.end(), [](const BodyCommand& c) { return c.kind == BodyCommand::Button; });
            };
            const auto tones = hardware.tone_count, buzzes = hardware.vibration_count;
            wheel_turn(-90, -40);
            require(dial_steps(0x4001) == 3 && buttons_sent() == 0,
                    "A 50° clockwise turn sends three positive wheel steps and no direction press");
            require(hardware.vibration_count > buzzes && hardware.tone_count > tones, "Each wheel step gives feedback");
            screenshot("sony-v4-stage5-remote");
            sent.clear();
            wheel_turn(-90, -125);
            require(dial_steps(0x4001) == -2 && buttons_sent() == 0, "Counter-clockwise turns send negative steps");
            sent.clear();
            auto top = point(-90, 80), right = point(0, 80), down = point(90, 80), left = point(180, 80);
            const auto beforeTaps = hardware.vibration_count;
            tap_at(top.first, top.second); tap_at(right.first, right.second);
            tap_at(down.first, down.second); tap_at(left.first, left.second); tap_at(cx, cy);
            require(sent.size() == 5 && sent[0].id == 1 && sent[1].id == 4 && sent[2].id == 2 && sent[3].id == 3 &&
                    sent[4].id == 5 && buttons_sent() == 5, "Taps send Up, Right, Down, Left and SET presses");
            require(hardware.vibration_count == beforeTaps + 5, "Every press gives one vibration");
            sent.clear();
            keyB();
            require(sent.size() == 1 && sent[0].kind == BodyCommand::Button && sent[0].id == 5, "KEYB sends SET");
            camera_click("MENU"); camera_click("Fn"); camera_click("回放");
            require(sent.size() == 4 && sent[1].id == 6 && sent[2].id == 0x10 && sent[3].id == 0x11,
                    "MENU, Fn and playback send their camera button IDs");
            hardware.speaker_volume = 0;
            const auto silentTones = hardware.tone_count, silentBuzz = hardware.vibration_count;
            tap_at(cx, cy);
            require(hardware.tone_count == silentTones && hardware.vibration_count == silentBuzz + 1,
                    "Volume zero silences body feedback but keeps vibration");
            hardware.speaker_volume = 40;
            body.body.resultId = 1;
            body.body.result = 0x2019;
            sony::preview::report(body); poll(false, false, 50);
            require(find_text(lv_screen_active(), "相机忙，请稍候"), "A refused body command is reported");
            camera_click("拨轮");
            require(find_text(lv_screen_active(), "前拨轮") && find_text(lv_screen_active(), "后拨轮") &&
                    find_text(lv_screen_active(), "C1") && find_text(lv_screen_active(), "AF-ON") &&
                    !find_text(lv_screen_active(), "SET"), "Dial page has only dials and custom keys");
            screenshot("sony-v4-stage5-dial");
            sent.clear();
            const auto idleTones = hardware.tone_count, idleBuzz = hardware.vibration_count;
            keyB();
            require(sent.empty() && hardware.tone_count == idleTones && hardware.vibration_count == idleBuzz,
                    "KEYB on the dial page has no action or feedback");
            tap_at(358, 181); tap_at(110, 181);
            require(sent.size() == 2 && sent[0].id == 0x4002 && sent[0].steps == 1 && sent[1].steps == -1,
                    "Front + and − send single detents");
            sent.clear();
            input_history.push({true, 358, 292, lv_tick_get()});
            poll(false, false, 720);
            input_history.push({false, 358, 292, lv_tick_get()});
            poll(false, false, 80);
            require(sent.size() >= 3 && sent.size() <= 4 && dial_steps(0x4003) == int(sent.size()),
                    "Holding rear + repeats single detents without an extra release step");
            sent.clear();
            auto swipe = [&](int16_t from, int16_t to) {
                auto t = lv_tick_get();
                for (int x = from;; x += to > from ? 10 : -10) {
                    if ((to > from && x > to) || (to < from && x < to)) x = to;
                    input_history.push({true, int16_t(x), 181, t += 16});
                    if (x == to) break;
                }
                input_history.push({false, to, 181, t += 16});
                poll(false, false, unsigned(std::abs(to - from) / 10 * 16 + 120));
            };
            swipe(180, 270);
            require(dial_steps(0x4002) == 3, "A 90 px rightward swipe sends three front-dial detents");
            sent.clear();
            swipe(270, 200);
            require(dial_steps(0x4002) == -2, "A 70 px leftward swipe sends two negative detents");
            sent.clear();
            camera_click("C1"); camera_click("C3"); camera_click("AF-ON");
            require(sent.size() == 3 && sent[0].id == 0x14 && sent[1].id == 0x16 && sent[2].id == 0x1c,
                    "Custom keys send C1, C3 and AF-ON");
            const auto clears = sony::preview::bodyClears;
            keyA();
            require(find_text(lv_screen_active(), "MENU") && sony::preview::bodyClears == clears + 1,
                    "KEYA returns to the remote page and drops queued input");
            auto limited = body;
            limited.body.buttons = {1, 5};
            limited.body.dials = {0x4001};
            sony::preview::report(limited); poll(false, false, 50);
            require(!find_text(lv_screen_active(), "MENU") && !find_text(lv_screen_active(), "拨轮") &&
                    find_text(lv_screen_active(), "SET"), "Only controls the camera lists remain");
            sent.clear();
            tap_at(left.first, left.second);
            tap_at(top.first, top.second);
            require(sent.size() == 1 && sent[0].id == 1, "Unlisted directions send nothing");
            auto recording = limited;
            recording.capture.recording = sony::Recording::Active;
            recording.capture.since = lv_tick_get();
            sony::preview::report(recording); poll(false, false, 50);
            require(find_text(lv_screen_active(), "REC") && toolbox::touch_input_blocked() && hardware.camera_muted,
                    "Body-initiated recording locks the remote page");
            sent.clear();
            wheel_turn(-90, -40);
            keyA();
            require(sent.empty() && find_text(lv_screen_active(), "REC"), "Recording blocks wheel input and KEYA");
            sony::preview::report(limited); poll(false, false, 50);
            require(!toolbox::touch_input_blocked() && find_text(lv_screen_active(), "KEYB 开始录像"),
                    "Confirmed idle restores the movie page");
            keyA();
            sony::preview::report(ready); poll(false, false, 50);
            require(find_text(lv_screen_active(), "Wi-Fi 已连接") && !find_text(lv_screen_active(), "机身"),
                    "Console hides body controls when the camera lists none");
            require(sony::preview::commands.size() == captureBefore, "Body controls never issue capture requests");
        }
        {
            using sony::MediaState;
            auto album = ready;
            album.mediaAvailable = album.mediaDeleteAvailable = true;
            album.body.buttons = {1, 5};
            sony::preview::report(album); poll(false, false, 50);
            screenshot("sony-v4-stage6-console");
            const char* console_tiles[] = {"拍照", "录像", "机身", "相册"};
            for (int i = 0; i < 4; ++i) {
                auto* label = find_text(lv_screen_active(), console_tiles[i]);
                require(label, console_tiles[i]);
                auto* tile = lv_obj_get_parent(label);
                require(lv_obj_get_width(tile) == 156 && lv_obj_get_height(tile) == 104 &&
                        lv_obj_get_x(tile) == 72 + i % 2 * 166 && lv_obj_get_y(tile) == 140 + i / 2 * 114,
                        "The Sony console must keep its original 2 by 2 actions");
            }
            camera_click("拍照");
            screenshot("sony-v4-stage6-photo-entry");
            camera_click("录像");
            screenshot("sony-v4-stage6-movie-entry");
            camera_click("相册");
            require(find_text(lv_screen_active(), "正在读取相册"), "Movie album opens directly into loading");
            keyA();
            require(find_text(lv_screen_active(), "KEYB 开始录像"), "KEYA returns to the movie origin");
            keyA();
            auto list = std::make_shared<sony::MediaList>();
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            for (int n = 0; n < 9; ++n) {
                sony::MediaItem item;
                item.key = {n < 2 ? 42u : unsigned(42 + n), uint8_t(n == 1 ? 1 : 2)};
                item.created = now - 30000 - n * 5000;
                item.local = item.created + 28800000;
                sony::MediaFile file;
                file.id = 7;
                file.video = n == 1;
                file.path = n == 1 ? "A:/M4ROOT/C0001.MP4" : "A:/DCIM/DSC0000" + std::to_string(n + 1) + ".JPG";
                item.files.push_back(file);
                if (n == 0) {
                    file.id = 9;
                    file.path = "B:/DCIM/DSC00001.ARW";
                    item.files.push_back(file);
                }
                list->push_back(std::move(item));
            }
            // Use LVGL's bundled JPEG sample to exercise production decoding and RGB565 rendering.
            const auto source = std::filesystem::path(TOOLBOX_UI_SOURCE_DIR) / "../../components/lvgl/examples/libs/libjpeg_turbo/flower.jpg";
            std::ifstream file(source, std::ios::binary);
            const std::vector<uint8_t> jpg{std::istreambuf_iterator<char>(file), {}};
            auto image = sony::decodeThumbnail({jpg.data(), jpg.size()});
            require(image.pixels && image.width == 227 && image.height == 149, "The production JPEG decoder returns RGB565 pixels");
            const auto publish = [&] { sony::preview::report(album); poll(false, false, 60); };
            const auto tap_at = [&](int16_t x, int16_t y) {
                auto t = lv_tick_get();
                input_history.push({true, x, y, t});
                input_history.push({false, x, y, t + 40});
                poll(false, false, 90);
            };
            const auto begin_drag = [&](int16_t x, int16_t y) {
                input_history.push({true, x, y, lv_tick_get()});
                poll(false, false, 32);
            };
            const auto drag_to = [&](int16_t x, int16_t y) {
                input_history.push({true, x, y, lv_tick_get()});
                poll(false, false, 32);
            };
            const auto end_drag = [&](int16_t x, int16_t y, unsigned advanceBy = 240) {
                input_history.push({false, x, y, lv_tick_get()});
                poll(false, false, advanceBy);
            };
            const auto swipe = [&](int16_t x, int16_t y, int16_t dx, int16_t dy) {
                auto t = lv_tick_get();
                input_history.push({true, x, y, t});
                input_history.push({true, int16_t(x + dx), int16_t(y + dy), t + 32});
                input_history.push({false, int16_t(x + dx), int16_t(y + dy), t + 64});
                poll(false, false, 310);
            };
            album.media.status = MediaState::Ready;
            album.media.items = list;
            album.media.tiles[0] = {list->front().key, image};
            album.media.tiles[1] = {(*list)[1].key, image};
            album.media.tiles[2] = {(*list)[2].key, image};
            album.media.tiles[3] = {(*list)[3].key, {}}; // explicit preview failure
            publish();
            keyB();
            require(sony::preview::commands.back().action == CaptureAction::Photo, "A shutter request is queued");
            camera_click("相册");
            auto* currentName = find_text(lv_screen_active(), "DSC00001.JPG");
            require(currentName &&
                    find_text(lv_screen_active(), sony::mediaDate(list->front()).c_str()) &&
                    !find_text(lv_screen_active(), "正在读取相册") &&
                    sony::preview::mediaWindow[0] == list->front().key, "A warm review opens before the shutter or new catalog replies");
            std::string age = find_text(lv_screen_active(), "30秒前拍摄") ? "30秒前拍摄" : "31秒前拍摄";
            require(find_text(lv_screen_active(), age.c_str()), "The fixed reference begins at entry");
            publish();
            screenshot("sony-v4-stage6-latest-photo");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            poll(false, false, 60);
            require(find_text(lv_screen_active(), age.c_str()), "Waiting in review preserves the relative time");
            require(!find_text(lv_screen_active(), "<") && !find_text(lv_screen_active(), ">"),
                    "Review has no arrow buttons");
            tap_at(105, 205);
            require(sony::preview::mediaWindow[0] == list->front().key, "Tapping the preview does not switch it");
            swipe(265, 210, -80, 0);
            require(sony::preview::mediaWindow[0] == list->front().key, "The newest item has no neighbor to its right");
            lv_area_t beforeDrag{}, duringDrag{};
            lv_obj_get_coords(currentName, &beforeDrag);
            begin_drag(195, 210);
            flushed_pixels = 0;
            drag_to(226, 210);
            lv_obj_get_coords(currentName, &duringDrag);
            require(duringDrag.x1 - beforeDrag.x1 == 31 && sony::preview::mediaWindow[0] == list->front().key,
                    "The preview follows the finger before release");
            if (!full_render) require(flushed_pixels && flushed_pixels < 468 * 466,
                                      "A preview drag redraws only part of the screen in DIRECT mode");
            end_drag(226, 210);
            lv_obj_get_coords(currentName, &duringDrag);
            require(duringDrag.x1 == beforeDrag.x1 && sony::preview::mediaWindow[0] == list->front().key,
                    "A short drag settles back to its original position");
            begin_drag(165, 210);
            drag_to(255, 210);
            end_drag(255, 210, 50);
            lv_obj_get_coords(currentName, &duringDrag);
            require(duringDrag.x1 > beforeDrag.x1 + 90 && sony::preview::mediaWindow[0] == list->front().key,
                    "Release animates the preview before the selected item changes");
            poll(false, false, 185);
            require(sony::preview::mediaWindow[0] == (*list)[1].key && find_text(lv_screen_active(), "C0001.MP4") &&
                    find_text(lv_screen_active(), ("创建 " + sony::mediaDate((*list)[1])).c_str()),
                    "Swiping right selects the older movie on the left and shows its directory creation time");
            screenshot("sony-v4-stage6-latest-movie");
            swipe(265, 210, -80, 0);
            require(sony::preview::mediaWindow[0] == list->front().key, "Swiping left returns to the newer item");
            tap_at(375, 104);
            require(find_text(lv_screen_active(), "1 / 3") && sony::preview::mediaWindow[3] == (*list)[3].key,
                    "Gallery requests exactly four visible items");
            require(!find_text(lv_screen_active(), "<") && !find_text(lv_screen_active(), ">"),
                    "Gallery has no page arrows");
            screenshot("sony-v4-stage6-gallery");
            swipe(150, 175, 90, 0);
            require(find_text(lv_screen_active(), "1 / 3"), "A horizontal gallery swipe neither turns a page nor opens a tile");
            auto* tileText = find_text(lv_screen_active(), "无预览");
            require(tileText, "The failed thumbnail is represented by a placeholder");
            lv_obj_get_coords(tileText, &beforeDrag);
            begin_drag(305, 300);
            flushed_pixels = 0;
            drag_to(305, 232);
            lv_obj_get_coords(tileText, &duringDrag);
            require(duringDrag.y1 - beforeDrag.y1 == -68 && sony::preview::mediaWindow[0] == list->front().key,
                    "Gallery follows the finger vertically while requesting only the current page");
            if (!full_render) require(flushed_pixels && flushed_pixels < 468 * 466,
                                      "A gallery drag redraws only part of the screen in DIRECT mode");
            drag_to(305, 48);
            end_drag(305, 48);
            require(find_text(lv_screen_active(), "2 / 3") && sony::preview::mediaWindow[0] == (*list)[4].key &&
                    sony::preview::mediaWindow[3] == (*list)[7].key, "A long swipe advances only one page and loads it after settling");
            keyB();
            require(find_text(lv_screen_active(), "DSC00005.JPG"), "Gallery KEYB opens the selected item");
            tap_at(375, 104); swipe(145, 166, 0, 85); tap_at(140, 180);
            require(sony::preview::mediaWindow[0] == list->front().key, "A downward swipe and tile tap return to the selected photo");
            keyB();
            require(find_text(lv_screen_active(), "DSC00001.ARW") &&
                    find_text(lv_screen_active(), "卡槽 2 · RAW + JPEG · 全部 2 个文件"), "Both files and the slot appear in confirmation");
            screenshot("sony-v4-stage6-delete");
            keyA();
            require(!sony::preview::mediaRemoved && find_text(lv_screen_active(), age.c_str()),
                    "Cancellation does not delete and preserves the same relative time");
            keyB(); keyB();
            require(sony::preview::mediaRemoved.get() == &list->front() &&
                    !find_text(lv_screen_active(), "删除此媒体？") && find_text(lv_screen_active(), "正在删除") &&
                    sony::preview::mediaWindow[0] == (*list)[1].key,
                    "Confirming queues the original content and immediately opens its neighbor");
            camera_click("删除");
            album.media.pendingDeletes[0] = list->front().key;
            album.media.status = MediaState::Checking;
            publish();
            require(find_text(lv_screen_active(), "删除此媒体？") && find_text(lv_screen_active(), "C0001.MP4") &&
                    sony::preview::mediaRemoved.get() == &list->front(),
                    "A pending deletion cannot close or confirm the next dialog");
            keyB();
            require(sony::preview::mediaRemoved.get() == &(*list)[1] &&
                    sony::preview::mediaWindow[0] == (*list)[2].key, "The second deletion can queue before the first replies");
            auto remaining = std::make_shared<sony::MediaList>(list->begin() + 1, list->end());
            album.media.status = MediaState::Deleted;
            album.media.items = remaining;
            album.media.pendingDeletes[0] = (*list)[1].key;
            album.media.deleteResult = MediaState::Deleted;
            ++album.media.deleteRevision;
            publish();
            require(sony::preview::mediaWindow[0] == (*list)[2].key, "First success preserves the second pending deletion");
            album.media.pendingDeletes = {};
            album.media.status = MediaState::DeleteFailed;
            album.media.deleteResult = MediaState::DeleteFailed;
            album.media.deleteError = 0x2019;
            ++album.media.deleteRevision;
            publish();
            swipe(165, 210, -80, 0);
            require(find_text(lv_screen_active(), "相机忙，删除失败") && sony::preview::mediaWindow[0] == (*list)[1].key,
                    "A refused deletion restores the object to the correct position");
            screenshot("sony-v4-stage6-delete-failed");
            const auto closes = sony::preview::mediaCloses;
            keyA();
            require(sony::preview::mediaCloses == closes + 1 && find_text(lv_screen_active(), "KEYB 拍照"),
                    "Back exits the review immediately while leaving the capture cache warm");
            camera_click("相册");
            album.media.status = MediaState::Ready;
            album.media.deleteResult = MediaState::Closed;
            album.media.items = list;
            publish();
            require(!find_text(lv_screen_active(), age.c_str()) && sony::preview::mediaWindow[0] == list->front().key,
                    "Reopening takes a new clock reference and follows the latest item from the refreshed catalog");
            album.media.items = std::make_shared<sony::MediaList>();
            publish();
            require(find_text(lv_screen_active(), "暂无媒体"), "Empty album after resync");
            screenshot("sony-v4-stage6-empty");
            keyA();
            album.mediaAvailable = album.mediaDeleteAvailable = false;
            album.media = {};
            publish();
            keyA();
        }
        {
            using sony::AutoAction;
            using sony::AutoResult;
            auto view = ready;
            view.mediaAvailable = true;
            auto publish = [&] { sony::preview::report(view); poll(false, false, 50); };
            auto snap = [&](const char* name) {
                require_glyphs(lv_obj_get_child(lv_screen_active(), -1));
                screenshot(name);
            };
            sony::preview::automationCommands.clear();
            publish();
            snap("sony-v4-stage7-console");
            camera_click("自动化");
            require(!find_text(lv_screen_active(), "焦点堆栈"), "Unverified focus stacking has no entry");
            snap("sony-v4-stage7-automation");
            camera_click("间隔拍摄");
            snap("sony-v4-stage7-interval");
            input_history.push({true, 351, 179, lv_tick_get()});
            input_history.push({false, 351, 179, lv_tick_get() + 30});
            poll(false, false, 60);
            require(find_text(lv_screen_active(), "6"), "Touch sets the requested interval");
            keyB();
            require(sony::preview::automationCommands == std::vector{AutoAction::Interval} &&
                    sony::preview::intervalPlan.seconds == 6 && sony::preview::intervalPlan.shots == 30 &&
                    find_text(lv_screen_active(), "已完成 0 / 30 张"), "Start does not count a shot");
            keyB();
            require(sony::preview::automationCommands.size() == 1, "Repeated KEYB does not start another run");
            auto* count = find_text(lv_screen_active(), "已完成 0 / 30 张");
            view.automation = sony::Connection::instance().state().automation;
            view.automation.completed = 2;
            publish();
            require(find_text(lv_screen_active(), "已完成 2 / 30 张") == count,
                    "Confirmed progress updates the existing label");
            snap("sony-v4-stage7-interval-running");
            const auto cancels = sony::preview::automationCancels;
            keyA();
            require(sony::preview::automationCancels == cancels + 1 &&
                    find_text(lv_screen_active(), "已完成 2 / 30 张"), "KEYA stops and preserves confirmed progress");
            keyA();
            camera_click("参数预设");
            require(find_text(lv_screen_active(), "暂无已保存预设"), "No placeholder preset can be applied");
            camera_click("保存相机当前参数");
            require(sony::preview::automationCommands.back() == AutoAction::SavePreset,
                    "Saving is an explicit read request");
            auto preset = std::make_shared<sony::Preset>();
            std::strcpy(preset->model, "ILCE-7M5");
            preset->mode = 1;
            preset->count = 2;
            preset->entries[0] = {400, Parameter::Iso, 6};
            preset->entries[1] = {-300, Parameter::Ev, 3};
            view.automation = {AutoAction::SavePreset, AutoResult::Done, 0, 0, 0, preset};
            publish();
            snap("sony-v4-stage7-preset");
            camera_click("查看并应用预设");
            require(find_text(lv_screen_active(), "400") && sony::preview::automationCommands.size() == 2,
                    "Opening the review shows saved values and performs no writes");
            snap("sony-v4-stage7-preset-confirm");
            keyA();
            require(sony::preview::automationCommands.size() == 2, "KEYA cancels the review");
            camera_click("查看并应用预设");
            keyB();
            require(sony::preview::automationCommands.back() == AutoAction::ApplyPreset,
                    "KEYB applies only the reviewed preset");
            view.automation = {AutoAction::ApplyPreset, AutoResult::Done, 0, 1, 2, preset};
            publish();
            require(find_text(lv_screen_active(), "已核对 1 / 2 项"), "Partial readback is visible");
            snap("sony-v4-stage7-preset-result");
            keyA(); keyA(); keyA();
            view.mediaAvailable = false;
            view.capture.parameters.reset();
            view.automation = {};
            publish();
            require(!find_text(lv_screen_active(), "自动化"), "Unavailable automation has no console entry");
            view = ready;
            view.mediaAvailable = true;
            publish();
            camera_click("自动化"); camera_click("间隔拍摄");
            keyB();
            const auto beforeDisconnect = sony::preview::automationCommands.size();
            sony::ConnectionState disconnected;
            disconnected.phase = sony::ConnectionPhase::Connecting;
            sony::preview::report(disconnected); poll(false, false, 50);
            publish();
            require(find_text(lv_screen_active(), "自动化") &&
                    sony::preview::automationCommands.size() == beforeDisconnect,
                    "Reconnection returns to the console without resuming the run");
        }
        keyA();
        sony::preview::saved = true;
        keyB();
        require(find_text(lv_screen_active(), "正在连接"), "Saved profile must start reconnection");
        sony::ConnectionState failure;
        failure.phase = sony::ConnectionPhase::Failed;
        failure.message = "找不到相机或无法加入热点";
        sony::preview::report(failure); poll(false, false, 50);
        screenshot("sony-v4-failed");
        keyB();
        require(find_text(lv_screen_active(), "正在连接"), "KEYB retries after failure");
        keyA();
        keyA();
        sony::preview::saved = false;
        camera_click("蓝牙遥控");
        poll(false, false, 110);
        screenshot("camera-search");
        toolbox::camera::Device device;
        device.name = "Sony ILCE-7M4";
        device.address = {1, 2, 3, 4, 5, 6};
        toolbox::camera::preview.devices = {device};
        poll(false, false, 110); screenshot("camera-scan");
        auto* label = find_text(lv_screen_active(), device.name.c_str());
        require(label != nullptr, "Camera scan list must show discovered device");
        auto* button = lv_obj_get_parent(label);
        while (button && !lv_obj_has_flag(button, LV_OBJ_FLAG_CLICKABLE)) button = lv_obj_get_parent(button);
        require(button != nullptr, "Camera device must be selectable");
        lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
        poll(false, false, 80); screenshot("camera-modes");
        const int before = toolbox::camera::commands;
        auto click_mode = [&](const char* title) {
            auto* caption = find_text(lv_screen_active(), title);
            require(caption != nullptr, "Mode selection must have a dedicated page entry");
            lv_obj_send_event(lv_obj_get_parent(caption), LV_EVENT_CLICKED, nullptr);
            poll(false, false, 80);
        };
        auto* camera_screen = lv_obj_get_child(lv_screen_active(), -1);
        auto camera_back = [&] {
            for (uint32_t i = 0; i < lv_obj_get_child_count(camera_screen); ++i) {
                auto* child = lv_obj_get_child(camera_screen, i);
                if (!lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE) || !lv_obj_get_child_count(child)) continue;
                auto* icon = lv_obj_get_child(child, -1);
                if (lv_obj_check_type(icon, &lv_image_class) && lv_image_get_src(icon) == &toolbox_camera_back)
                    return child;
            }
            throw std::runtime_error("Camera page must have a back button");
        };
        auto* back_button = camera_back();
        auto* ring = lv_obj_get_child(camera_screen, 0);
        auto check_ring = [&](uint32_t color, int width) {
            require(lv_color_eq(lv_obj_get_style_arc_color(ring, LV_PART_MAIN), lv_color_hex(color)) &&
                    lv_obj_get_style_arc_width(ring, LV_PART_MAIN) == width &&
                    lv_color_eq(lv_obj_get_style_bg_color(camera_screen, LV_PART_MAIN), lv_color_black()),
                    "Camera state must use the expected ring on a black screen");
        };
        auto back = [&] {
            require(!lv_obj_has_flag(back_button, LV_OBJ_FLAG_HIDDEN), "Back must be available outside active recording");
            lv_obj_send_event(back_button, LV_EVENT_CLICKED, nullptr);
            poll(false, false, 80);
        };
        check_ring(0x697b95, 8);
        require(find_text(camera_screen, "10:09") && find_text(camera_screen, "82%"),
                "Camera header must show system time and development-board battery");
        poll(false, true, 40); poll(false, false, 80);
        check_ring(0x3c965e, 8);
        require(toolbox::camera::commands == before, "KEYB must enter movie mode without starting a recording");
        back();
        check_ring(0x697b95, 8);
        click_mode("拍照遥控");
        screenshot("camera-photo");
        require_glyphs(camera_screen);
        check_ring(0x697b95, 8);
        bool design_icon_visible = false;
        for (uint32_t i = 0; i < lv_obj_get_child_count(camera_screen); ++i) {
            auto* child = lv_obj_get_child(camera_screen, i);
            design_icon_visible |= lv_obj_check_type(child, &lv_image_class) &&
                !lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN) && lv_image_get_src(child) == &toolbox_camera_photo;
        }
        require(design_icon_visible, "The photo page must use the approved SVG camera icon");
        require(!find_text(camera_screen, "拍照") && find_text(camera_screen, "按 KEYB 拍照"),
                "Photo page must only show the physical shutter hint");
        input_history.push({true, 233, 314, lv_tick_get()});
        input_history.push({false, 233, 314, lv_tick_get() + 16});
        poll(false, false, 80);
        require(toolbox::camera::commands == before, "Tapping the former shutter area must do nothing");
        poll(true, false, 40); poll(false, false, 80);
        require(toolbox::camera::commands == before, "KEYA must be inactive on photo page");
        poll(false, true, 40);
        require(toolbox::camera::commands == before + 1 && toolbox::camera::last_command == toolbox::camera::Mode::Photo,
                "Pressing KEYB must fire the photo shutter before release");
        poll(false, false, 80);
        require(toolbox::camera::commands == before + 1, "KEYB release must not send a second shutter command");
        screenshot("camera-photo-sent");
        back();
        require(app.currentState() != mooncake::AppAbility::StateSleeping, "Control-page back must return to mode selection");
        click_mode("录像遥控");
        check_ring(0x3c965e, 8);
        require(!toolbox::camera::preview.recording_known && find_text(camera_screen, "待录制") &&
                find_text(camera_screen, "00:00:00") && find_text(camera_screen, "按 KEYB 开始录像"),
                "The first movie entry must be ready without a recording notification");
        screenshot("camera-movie-ready");
        poll(true, false, 40); poll(false, false, 80);
        require(toolbox::camera::commands == before + 1, "KEYA must be inactive on movie page");
        toolbox::camera::delay_movie_reply = true;
        poll(false, true, 40); poll(false, false, 110);
        check_ring(0xb58432, 8);
        require(!toolbox::touch_input_blocked() && find_text(camera_screen, "等待开录确认"),
                "A pending start must wait for the camera before locking the screen");
        screenshot("camera-movie-starting");
        toolbox::camera::preview.recording_known = true;
        toolbox::camera::preview.recording = true;
        toolbox::camera::preview.recording_since = hardware.millis();
        poll(false, false, 110);
        require(toolbox::camera::commands == before + 2 && toolbox::camera::preview.recording,
                "KEYB must start recording on the separate movie page");
        check_ring(0xbe3b49, 14);
        require(lv_obj_has_flag(back_button, LV_OBJ_FLAG_HIDDEN) && toolbox::touch_input_blocked(),
                "Recording must hide back and lock the touchscreen");
        lv_obj_send_event(back_button, LV_EVENT_CLICKED, nullptr);
        input_history.push({true, 233, 20, lv_tick_get()});
        input_history.push({true, 233, 130, lv_tick_get() + 20});
        input_history.push({false, 233, 130, lv_tick_get() + 40});
        const int before_first_second = 1490 - static_cast<int>(hardware.millis() - toolbox::camera::preview.recording_since);
        poll(false, false, before_first_second);
        require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN) && find_text(camera_screen, "00:00:00"),
                "Touches must do nothing and the first second must wait for the 500 ms offset");
        poll(false, false, 120);
        require(find_text(camera_screen, "00:00:01") != nullptr, "The first recorded second must appear after the offset");
        screenshot("camera-movie");
        require_glyphs(camera_screen);
        flushed_pixels = 0;
        poll(false, false, 300);
        require(flushed_pixels == 0, "A static recording frame should not repaint between elapsed seconds");
        toolbox::camera::preview.busy = true;
        poll(false, true, 40); poll(false, false, 80);
        require(toolbox::camera::commands == before + 2, "Busy camera must reject duplicate key actions");
        toolbox::camera::preview.busy = false;
        poll(false, true, 40); poll(false, false, 110);
        check_ring(0xbe3b49, 14);
        require(toolbox::camera::commands == before + 3 && toolbox::touch_input_blocked() &&
                find_text(camera_screen, "等待停录确认"),
                "A pending stop must retain the red ring and touch lock");
        screenshot("camera-movie-stopping");
        toolbox::camera::preview.recording = false;
        poll(false, false, 110);
        require(!toolbox::camera::preview.recording && !toolbox::touch_input_blocked(),
                "KEYB must stop recording and restore touch");
        check_ring(0xb58432, 8);
        toolbox::camera::preview.busy = toolbox::camera::delay_movie_reply = false;
        poll(false, false, 110);
        check_ring(0x3c965e, 8);
        const int commands_before_exit = toolbox::camera::commands;
        poll(true, false, 650); poll(false, false, 80);
        require(app.currentState() != mooncake::AppAbility::StateSleeping && toolbox::camera::commands == commands_before_exit,
                "Holding KEYA must have no camera or navigation action");
        back(); back();
        require(app.currentState() == mooncake::AppAbility::StateSleeping &&
                toolbox::camera::commands == commands_before_exit && toolbox::camera::preview.phase == toolbox::camera::Phase::Off,
                "Touch back must close camera without issuing shutter or movie commands");
    }
    check_extensions();
    {
        AppCamera app;
        hardware.rawA = hardware.rawB = false;
        app.baseCreate(); app.open(); app.baseUpdate();
        auto poll = [&](bool a, bool b, int ms) {
            hardware.rawA = a; hardware.rawB = b;
            for (int t = 0; t < ms; t += 10) { advance(10); app.baseUpdate(); }
        };
        sony::preview::saved = true;
        auto keyB = [&] { poll(false, true, 80); poll(false, false, 100); };
        keyB();
        sony::ConnectionState ready;
        ready.phase = sony::ConnectionPhase::Ready;
        ready.model = "ILCE-7M5";
        ready.mediaAvailable = ready.capture.canPhoto = true;
        ready.capture.recording = sony::Recording::Idle;
        auto params = std::make_shared<sony::Parameters>();
        params->push_back({sony::Parameter::Drive, 6, 1});
        ready.capture.parameters = params;
        sony::preview::report(ready); poll(false, false, 50);
        for (const char* caption : {"自动化", "间隔拍摄"}) {
            auto* text = find_text(lv_screen_active(), caption);
            require(text, caption);
            lv_obj_send_event(lv_obj_get_parent(text), LV_EVENT_CLICKED, nullptr);
            poll(false, false, 50);
        }
        keyB();
        const auto beforeExit = sony::preview::automationCancels;
        poll(true, true, 690);
        require(app.currentState() == mooncake::AppAbility::StateSleeping &&
                sony::preview::automationCancels == beforeExit + 1, "A+B exits and cancels the active run");
        hardware.rawA = hardware.rawB = false;
        sony::preview::saved = false;
    }
    toolbox::shell_detach();
    check_scroll_cache();
    check_drawer_persistence();
    check_drawer(8);
    check_drawer(20);
    check_lighting();
    std::cout << "Passed camera UI and real KEYA/KEYB paths, buffered flick/list swipe/tap/cancel, DMA area/stride/RGB565 staging, unlock snapshot lifecycle, LVGL render, inertia, menu clearance, battery fill, overlay timing and real stopwatch button-path checks\n";
    return 0;
}
int main(int argc, char** argv)
{
    full_render = argc > 1 && std::string_view(argv[1]) == "full";
    lighting_render = argc > 1 && std::string_view(argv[1]) == "lighting";
    capture_dir = std::filesystem::absolute(argv[0]).parent_path() / "images";
    try {
        return render_all();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
