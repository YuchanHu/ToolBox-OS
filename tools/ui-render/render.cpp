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
#include <hal/display_frame.h>
#include <hal/utils/settings/settings.h>
#include <src/misc/lv_text_private.h>
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
void screenshot(const char* name)
{
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
        require(find_text(lv_screen_active(), "第1圈") != nullptr, "Recorded laps must be visible");
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
    toolbox::shell_detach();
    check_scroll_cache();
    check_drawer_persistence();
    check_drawer(8);
    check_drawer(20);
    std::cout << "Passed camera UI and real KEYA/KEYB paths, buffered flick/list swipe/tap/cancel, DMA area/stride/RGB565 staging, unlock snapshot lifecycle, LVGL render, inertia, menu clearance, battery fill, overlay timing and real stopwatch button-path checks\n";
    return 0;
}
int main(int argc, char** argv)
{
    full_render = argc > 1 && std::string_view(argv[1]) == "full";
    capture_dir = std::filesystem::absolute(argv[0]).parent_path() / "images";
    try {
        return render_all();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
