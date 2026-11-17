#include <system/shell.h>
#include <system/touch_input.h>
#include <system/runtime.h>
#include <system/ble.h>
#include <system/stopwatch_ui.h>
#include <apps/app_launcher/view/view.h>
#include <apps/app_stopwatch/app_stopwatch.h>
#include <apps/app_camera/app_camera.h>
#include <hal/hal.h>
#include <hal/display_frame.h>
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
    std::ofstream output(std::string("build-ui-render/images/") + name + ".bgra", std::ios::binary);
    output.write(reinterpret_cast<char*>(framebuffer.data()), framebuffer.size() * 4);
}
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check_display_transfer()
{
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
void check_extensions();
int render_all()
{
    check_display_transfer();
    std::filesystem::create_directories("build-ui-render/images");
    lv_init();
    auto* display = lv_display_create(468, 466);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(display, draw_buffer.data(), second_draw_buffer.data(), draw_buffer.size() * 2,
        full_render ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* display, const lv_area_t*, uint8_t* pixels) {
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
    auto* boot = toolbox::ui::boot(lv_screen_active());
    lv_obj_set_x(boot, 1);
    screenshot("boot");
    lv_obj_delete(boot);
    toolbox::shell_init();
    view::LauncherView drawer;
    std::vector<mooncake::AppProps_t> apps(3);
    apps[0].info.name = "秒表"; apps[1].info.name = "设置"; apps[2].info.name = "相机遥控";
    drawer.init(apps);
    toolbox::shell_attach(drawer.root());
    advance(40);
    screenshot("home");
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
    hardware.charging = true;
    advance(1100);
    auto* status_text = find_text(lv_screen_active(), "82%");
    require(status_text != nullptr, "Charging must retain the percentage without a plus suffix");
    auto* status_row = lv_obj_get_parent(status_text);
    auto* charging_mark = lv_obj_get_child(status_row, -1);
    require(!lv_obj_has_flag(charging_mark, LV_OBJ_FLAG_HIDDEN) &&
            lv_obj_check_type(lv_obj_get_child(charging_mark, 0), &lv_line_class),
            "Charging must show the lightning line icon");
    screenshot("home-charging");
    hardware.charging = false;
    advance(1100);
    require(lv_obj_has_flag(charging_mark, LV_OBJ_FLAG_HIDDEN), "Lightning must hide when charging ends");

    toolbox::shell_touch(true, 233, 320);
    advance(16);
    toolbox::shell_touch(true, 233, 230);
    auto* animated_face = lv_obj_get_child(lv_screen_active(), -1);
    require(lv_obj_get_style_image_opa(animated_face, LV_PART_MAIN) == 128,
            "90px drag must reach half of the extended 180px visual travel");
    toolbox::shell_cancel_touch();
    advance(300);
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
    const unsigned screen_children = lv_obj_get_child_count(lv_screen_active());
    toolbox::shell_touch(true, 233, 300);
    advance(20);
    toolbox::shell_touch(true, 233, 260);
    require(lv_obj_get_child_count(lv_screen_active()) == screen_children + 2,
            "Unlock should use two cached page images");
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
    drawer.update(); screenshot("drawer");
    toolbox::shell_lock();
    swipe(233, 20, 100); advance(220); screenshot("clock");
    auto* overlay = lv_obj_get_child(lv_layer_top(), 1);
    advance(1200);
    require(!lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Clock disappeared early");
    advance(310);
    require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Clock must close after 1.5 seconds");
    swipe(233, 20, 100); advance(220);
    swipe(233, 60, 140); advance(220); screenshot("control");
    swipe(233, 380, 280); advance(220);
    require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Control upward swipe must close directly");
    advance(1600);
    require(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN), "Clock must stay hidden after control dismissal");
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
    input_history.push({false, 233, 180, captured_at + 320});
    advance(400);
    require(find_text(settings_page, "Bluetooth") != nullptr, "Overflow must cancel without a menu click");
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
    toolbox::shell_close_settings(); screenshot("analog");
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
        poll(false, false, 80);
        toolbox::camera::Device device;
        device.name = "Sony ILCE-7M4";
        device.address = {1, 2, 3, 4, 5, 6};
        toolbox::camera::preview.devices = {device};
        poll(false, false, 80); screenshot("camera-scan");
        auto* label = find_text(lv_screen_active(), device.name.c_str());
        require(label != nullptr, "Camera scan list must show discovered device");
        auto* button = lv_obj_get_parent(label);
        while (button && !lv_obj_has_flag(button, LV_OBJ_FLAG_CLICKABLE)) button = lv_obj_get_parent(button);
        require(button != nullptr, "Camera device must be selectable");
        lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
        poll(false, false, 80); screenshot("camera-modes");
        const int before = toolbox::camera::commands;
        poll(false, true, 40); poll(false, false, 80);
        require(toolbox::camera::commands == before, "KEYB must do nothing on mode selection page");
        auto click_mode = [&](const char* title) {
            auto* caption = find_text(lv_screen_active(), title);
            require(caption != nullptr, "Mode selection must have a dedicated page entry");
            lv_obj_send_event(lv_obj_get_parent(caption), LV_EVENT_CLICKED, nullptr);
            poll(false, false, 80);
        };
        auto back = [&]() {
            auto* title = find_text(lv_screen_active(), "拍照遥控");
            if (!title || lv_obj_get_y(title) != 47) title = find_text(lv_screen_active(), "录像遥控");
            if (!title || lv_obj_get_y(title) != 47) title = find_text(lv_screen_active(), "相机遥控");
            require(title != nullptr && lv_obj_get_y(title) == 47, "Camera page must have standard header");
            lv_obj_send_event(lv_obj_get_child(lv_obj_get_parent(title), 1), LV_EVENT_CLICKED, nullptr);
            poll(false, false, 80);
        };
        click_mode("拍照遥控");
        screenshot("camera-photo");
        poll(true, false, 40); poll(false, false, 80);
        require(toolbox::camera::commands == before, "KEYA must be inactive on photo page");
        poll(false, true, 40); poll(false, false, 80);
        require(toolbox::camera::commands == before + 1 && toolbox::camera::last_command == toolbox::camera::Mode::Photo,
                "KEYB must fire photo shutter once");
        back();
        require(app.currentState() != mooncake::AppAbility::StateSleeping, "Control-page back must return to mode selection");
        click_mode("录像遥控");
        poll(true, false, 40); poll(false, false, 80);
        require(toolbox::camera::commands == before + 1, "KEYA must be inactive on movie page");
        poll(false, true, 40); poll(false, false, 1100);
        require(toolbox::camera::commands == before + 2 && toolbox::camera::preview.recording,
                "KEYB must start recording on the separate movie page");
        screenshot("camera-movie");
        toolbox::camera::preview.busy = true;
        poll(false, true, 40); poll(false, false, 80);
        require(toolbox::camera::commands == before + 2, "Busy camera must reject duplicate key actions");
        toolbox::camera::preview.busy = false;
        poll(false, true, 40); poll(false, false, 80);
        require(!toolbox::camera::preview.recording, "KEYB must stop recording in movie mode");
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
    std::cout << "Passed camera UI and real KEYA/KEYB paths, buffered flick/list swipe/tap/cancel, DMA area/stride/RGB565 staging, unlock snapshot lifecycle, LVGL render, inertia, menu clearance, battery fill, overlay timing and real stopwatch button-path checks\n";
    return 0;
}
int main(int argc, char**) { full_render = argc > 1; try { return render_all(); } catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; } }
