#include <apps/app_lighting/app_lighting.h>
#include <apps/app_launcher/view/view.h>
#include <system/touch_input.h>
#include <cstdio>
#include <iostream>

extern bool full_render;
extern unsigned flushed_pixels;
extern toolbox::TouchHistory input_history;
void advance(unsigned milliseconds);
void require(bool value, const char* message);
void screenshot(const char* name);
lv_obj_t* find_text(lv_obj_t* root, const char* value);
void require_menu_row(lv_obj_t* root, const char* caption);

namespace toolbox::lighting {
namespace {
RadioState preview;
Device fixture;
std::vector<State> sent;
}
const char *brand_name(Brand brand) {
    constexpr const char *names[] = {"影器", "永诺", "爱图仕 / amaran", "LYD / 轻装时代", "神牛", "印迹",
                                     "金贝", "新摄角", "南光", "Ulanzi", "智云"};
    return names[static_cast<int>(brand)];
}
bool is_broadcast(Brand brand) { return brand == Brand::Inkee || brand == Brand::Lyd; }
void start(Brand brand) {
    preview = {};
    preview.phase = Phase::Scanning;
    preview.message = "正在扫描";
    if (fixture.brand != brand)
        return;
    preview.devices.push_back(fixture);
    if (brand == Brand::Yongnuo) {
        const char* names[]{"YN灯光（Ａ）🧪\xF0\x9F", "YN360 IV", "YNLUX100RGB"};
        for (const auto* name : names) {
            auto device = fixture;
            device.address.back() = static_cast<uint8_t>(preview.devices.size());
            std::snprintf(device.name, sizeof(device.name), "%s", name);
            preview.devices.push_back(device);
        }
    }
}
void stop() { preview = {}; }
void tick() {}
RadioState status() { return preview; }
void connect(const Device& device) {
    preview.selected = device;
    preview.selected_valid = true;
    preview.channel_known = device.channel != 0;
    preview.phase = Phase::Ready;
    preview.message = is_broadcast(device.brand) ? "广播控制" : "已连接";
}
void set_channel(uint16_t channel) { preview.selected.channel = channel; }
void send(const State& state, bool) { if (preview.selected_valid) sent.push_back(state); }
void pair_inkee() {}
}

void check_lighting()
{
    using namespace toolbox::lighting;
    {
        AppLighting app;
        mooncake::AppProps_t props;
        props.appID = 1;
        props.info = app.getAppInfo();
        view::LauncherView drawer;
        drawer.init({props});
        advance(40);
        require(find_text(drawer.root(), "灯光控制"), "Drawer must display the production app name");
        screenshot("lighting-drawer");
    }
    auto scenario = [](Brand brand, const char* model, Capability caps, const char* image, int cct_step = 50) {
        fixture = {};
        fixture.brand = brand;
        fixture.caps = caps;
        fixture.channel = brand == Brand::Yongnuo ? 12 : 0;
        std::snprintf(fixture.name, sizeof(fixture.name), "%s", model);
        sent.clear();
        AppLighting app;
        app.baseCreate(); app.open(); app.baseUpdate();
        auto poll = [&](unsigned time) {
            for (unsigned elapsed = 0; elapsed < time; elapsed += 10) { advance(10); app.baseUpdate(); }
        };
        auto click = [&](const char* text) {
            auto* label = find_text(lv_screen_active(), text);
            require(label, text);
            lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, nullptr);
            poll(130);
        };
        auto touch = [&](bool pressed, int x, int y, unsigned time) {
            input_history.push({pressed, static_cast<int16_t>(x), static_cast<int16_t>(y), lv_tick_get()});
            poll(time);
        };
        if (brand == Brand::Yongnuo) {
            lv_obj_update_layout(lv_screen_active());
            auto* add = find_text(lv_screen_active(), "添加灯具");
            require(add, "Add fixture entry");
            auto* button = lv_obj_get_parent(add);
            require(lv_obj_get_width(button) >= 210 && lv_obj_get_height(button) >= 52, "Larger add fixture button");
            screenshot("lighting-fixtures");
        }
        click("添加灯具");
        auto check_four_rows = [&](const char* caption, bool scroll = false) {
            require_menu_row(lv_screen_active(), caption);
            auto* text = find_text(lv_screen_active(), caption);
            auto* option = lv_obj_get_parent(text);
            auto* list = lv_obj_get_parent(option);
            if (scroll) lv_obj_scroll_to_view_recursive(option, LV_ANIM_OFF);
            lv_area_t option_bounds, list_bounds;
            lv_obj_get_coords(option, &option_bounds);
            lv_obj_get_coords(list, &list_bounds);
            require(option_bounds.y1 >= list_bounds.y1 && option_bounds.y2 <= list_bounds.y2,
                    "The fourth menu option must be fully reachable");
            if (scroll) lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
        };
        if (brand == Brand::Yongnuo) {
            check_four_rows("LYD / 轻装时代");
            screenshot("lighting-brands");
            auto* last = find_text(lv_screen_active(), "智云");
            require(last, "Last lighting brand");
            auto* row = lv_obj_get_parent(last);
            lv_obj_scroll_to_view_recursive(row, LV_ANIM_OFF);
            screenshot("lighting-brands-more");
            lv_obj_scroll_to_y(lv_obj_get_parent(row), 0, LV_ANIM_OFF);
        }
        click(brand_name(brand));
        if (brand == Brand::Yongnuo) {
            require(find_text(lv_screen_active(), "YN灯光(A)"), "Normalize malformed and unsupported BLE name glyphs");
            check_four_rows("YNLUX100RGB", true);
            screenshot("lighting-scan");
        }
        click(is_broadcast(brand) ? "创建灯位" : model);
        require(find_text(lv_screen_active(), "亮度"), "Missing brightness picker");
        const bool cct = caps.cct && caps.min_kelvin < caps.max_kelvin;
        require(bool(find_text(lv_screen_active(), "色温")) == cct, "Temperature visibility");
        require(bool(find_text(lv_screen_active(), "设置 CH")) == (brand == Brand::Yongnuo), "CH visibility");
        require(bool(find_text(lv_screen_active(), "G/M +0")) == (caps.min_tint < caps.max_tint), "G/M visibility");
        require(sent.empty(), "Opening a lamp must not send control values");

        const int ruler_y = cct ? 189 : 227;
        lv_refr_now(nullptr);
        flushed_pixels = 0;
        touch(true, 315, ruler_y, 30);
        for (int x = 310; x >= 165; x -= 5) touch(true, x, ruler_y, 30);
        if (caps.decimal) touch(true, 164, ruler_y, 30);
        require(find_text(lv_screen_active(), caps.decimal ? "65.1%" : "65%"), "Ruler precision or drag direction");
        if (!full_render) require(flushed_pixels < 31u * 466u * 466u / 3, "Drag must redraw a local area");
        require(sent.empty(), "Moving the ruler must not write per frame");
        touch(false, caps.decimal ? 164 : 165, ruler_y, 140);
        require(sent.size() == 1 && sent.back().brightness == (caps.decimal ? 651 : 650), "Final brightness write");
        if (image) screenshot(image);

        touch(true, 270, ruler_y, 20); touch(true, 85, ruler_y, 20); touch(false, 85, ruler_y, 140);
        require(sent.back().brightness == 1000, "Accelerated brightness drag must clamp to 100%");
        touch(true, 85, ruler_y, 20); touch(true, 380, ruler_y, 20); touch(false, 380, ruler_y, 140);
        require(sent.back().brightness == 0, "Brightness must clamp to 0%");
        if (cct) {
            constexpr int cct_y = 290;
            const size_t before = sent.size();
            touch(true, 260, cct_y, 30); touch(true, 260, cct_y - 40, 30);
            touch(false, 260, cct_y - 40, 140);
            require(sent.size() == before, "Vertical motion must not change the CCT scale");
            touch(true, 315, cct_y, 30); touch(false, 315, cct_y, 140);
            require(sent.size() == before, "CCT must use relative movement");

            touch(true, 280, cct_y, 30); touch(true, 273, cct_y, 30);
            const auto warmer = std::to_string(5600 + cct_step) + " K";
            require(find_text(lv_screen_active(), warmer.c_str()), "Moving CCT must update the visible value");
            require(sent.size() == before, "Moving the CCT scale must not write per frame");
            touch(false, 273, cct_y, 50);
            require(sent.size() == before, "The CCT write must use the existing debounce");
            poll(90);
            require(sent.size() == before + 1 && sent.back().kelvin == 5600 + cct_step && sent.back().brightness == 0,
                    "Left CCT drag must snap to one model step independently of brightness");
            touch(true, 240, cct_y, 30); touch(true, 247, cct_y, 30); touch(false, 247, cct_y, 140);
            require(sent.size() == before + 2 && sent.back().kelvin == 5600, "Right CCT drag must lower kelvin");

            auto swipe = [&](int from, int to) {
                touch(true, from, cct_y, 30); touch(true, to, cct_y, 20); touch(false, to, cct_y, 140);
            };
            for (int i = 0; i < 3; ++i) swipe(360, 88);
            require(sent.back().kelvin == caps.max_kelvin, "CCT scale must clamp at model maximum");
            const size_t at_max = sent.size();
            swipe(360, 88);
            require(sent.size() == at_max, "CCT scale must not wrap from maximum");
            for (int i = 0; i < 3; ++i) swipe(88, 360);
            require(sent.back().kelvin == caps.min_kelvin, "CCT scale must clamp at model minimum");
            const size_t at_min = sent.size();
            swipe(88, 360);
            require(sent.size() == at_min, "CCT scale must not wrap from minimum");
        }
        if (brand == Brand::Sidus && caps.hsi) {
            click("CCT");
            require_menu_row(lv_screen_active(), "HSI");
            screenshot("lighting-modes");
            click("HSI");
            require(find_text(lv_screen_active(), "0°"), "HSI must show the degree sign");
            screenshot("lighting-hsi");
            for (int i = 0; i < 2; ++i) {
                touch(true, 82, 104, 30);
                touch(false, 82, 104, 40);
            }
            require_menu_row(lv_screen_active(), model);
            screenshot("lighting-fixtures-populated");
        }
        app.close(); app.baseUpdate();
        require(status().phase == Phase::Off, "Closing lighting must stop its radio session");
    };
    scenario(Brand::Inkee, "影器 1", {}, "lighting-inkee");
    scenario(Brand::Yongnuo, "YN150ULTRABI", {2700, 6500, 0, 0, true, false, false, true}, "lighting-yongnuo");
    scenario(Brand::Sidus, "amaran P60c", find_product(Brand::Sidus, "40015")->caps, "lighting-sidus");
    scenario(Brand::Sidus, "amaran 100d", find_product(Brand::Sidus, "40025")->caps, "lighting-fixed");
    scenario(Brand::Zhiyun, "MOLUS G60", find_product(Brand::Zhiyun, "PL103")->caps, nullptr, 100);
    std::cout << "Passed lighting LVGL horizontal scales, 50/100 K and 0.1% precision, endpoints, debounce and lifecycle\n";
}
