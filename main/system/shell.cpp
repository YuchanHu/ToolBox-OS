#include "shell.h"
#include "fonts.h"
#include "ui.h"
#include "ble.h"
#include "runtime.h"
#include <src/misc/cache/instance/lv_image_cache.h>
#include <hal/hal.h>
#include <hal/refresh_metrics.h>
#include <hal/utils/settings/settings.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <string>
#include <atomic>

namespace toolbox {
namespace {
constexpr uint32_t accent = 0x9dc6ff;
constexpr int unlock_distance = 180;
lv_obj_t *drawer, *lockscreen, *digital, *date_label, *status, *dial;
lv_obj_t *overlay, *clock_panel, *clock_time, *clock_battery, *controls;
lv_obj_t *control_battery, *wifi_button, *settings, *dial_date, *dial_battery;
lv_obj_t *brightness, *volume, *ble_button, *network_text;
lv_obj_t *network_name = nullptr;
lv_obj_t *control_time, *brightness_value, *volume_value, *battery_arc;
lv_obj_t *lock_wifi, *lock_battery, *dial_day, *lock_status_row;
lv_obj_t *clock_battery_icon, *control_battery_icon;
lv_obj_t *lock_charge, *clock_charge, *control_charge;
lv_obj_t *unlock_lock_image = nullptr, *unlock_drawer_image = nullptr;
lv_draw_buf_t *unlock_lock_pixels = nullptr, *unlock_drawer_pixels = nullptr;
float unlock_velocity = 0;
bool motion_sampled = false;
int motion_y = 0;
uint32_t motion_tick = 0;
lv_obj_t *wifi_switch = nullptr;
enum class Page { None, Settings, Wifi, Ap, Faces, Ble, Display, Sound, Info, Power, Phone };
Page page = Page::None;
std::function<void()> settings_closed;
uint32_t clock_shown_at = 0;
bool overlay_closing = false;
std::array<std::array<lv_point_precise_t, 2>, 60> tick_points;
void render_page(Page target);
void update_time();
lv_obj_t *ble_status = nullptr;
bool pending_ble = false, desired_ble = false;
bool pending_provision = false, desired_provision = false;
bool provisioning_visible = false;
std::array<lv_obj_t*, 3> hands;
std::array<std::array<lv_point_precise_t, 2>, 3> hand_points;
bool locked = true, analog = false, settling = false;
int overlay_level = 0;
int unlock_progress = 0;
int rendered_progress = -1;
bool lock_snapshot_dirty = true;
bool drawer_snapshot_dirty = false;
std::array<int, 3> hand_steps{-1, -1, -1};
uint32_t last_second = UINT32_MAX;
bool was_pressed = false, captured = false, unlock_dragging = false;
int start_x, start_y, last_x, last_y;
bool pending_brightness = false, pending_volume = false, pending_wifi = false;
bool save_brightness = false, save_volume = false;
int desired_brightness = 0, desired_volume = 0;
bool desired_wifi = false;
bool pending_face = false;
std::atomic<uint32_t> activity_tick{0};
std::atomic<int> home_seconds{60}, power_seconds{600};
bool resume_drawer = false;
uint32_t shift_tick = 0;

using ui::panel;
using ui::text;
using ui::icon;
using ui::button;
using ui::icon_button;

void set_analog(bool enabled)
{
    analog = enabled;
    if (!lockscreen) return;
    lv_obj_set_flag(dial, LV_OBJ_FLAG_HIDDEN, !analog);
    lv_obj_set_flag(digital, LV_OBJ_FLAG_HIDDEN, analog);
    lv_obj_set_flag(date_label, LV_OBJ_FLAG_HIDDEN, analog);
    lv_obj_set_flag(lock_status_row, LV_OBJ_FLAG_HIDDEN, analog);
    last_second = UINT32_MAX;
    lock_snapshot_dirty = true;
}

void overlay_position(void* object, int32_t value)
{
    lv_obj_set_y(static_cast<lv_obj_t*>(object), value);
}

void overlay_hidden(lv_anim_t*)
{
    overlay_closing = false;
    if (overlay_level == 0) lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}

void show_overlay(int level)
{
    const int previous = overlay_level;
    if (level == 0 && previous == 0) {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    overlay_level = level;
    last_second = UINT32_MAX;
    lv_obj_set_style_bg_opa(overlay, level == 1 ? 85 : LV_OPA_TRANSP, 0);
    lv_anim_delete(clock_panel, overlay_position);
    lv_anim_delete(controls, overlay_position);
    auto* surface = (level == 2 || previous == 2) ? controls : clock_panel;
    const int height = surface == controls ? 466 : 132;
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flag(clock_panel, LV_OBJ_FLAG_HIDDEN, surface != clock_panel);
    lv_obj_set_flag(controls, LV_OBJ_FLAG_HIDDEN, surface != controls);
    overlay_closing = level == 0;
    if (level == 1) clock_shown_at = lv_tick_get();
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, surface);
    lv_anim_set_values(&animation, level ? -height : lv_obj_get_y(surface), level ? 0 : -height);
    lv_anim_set_duration(&animation, 200);
    lv_anim_set_exec_cb(&animation, overlay_position);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, overlay_hidden);
    lv_anim_start(&animation);
    lv_obj_move_foreground(overlay);
    if (level) update_time();
    if (GetHAL().lvTouchpad) lv_indev_reset(GetHAL().lvTouchpad, nullptr);
}

void release_unlock_images()
{
    if (unlock_lock_image) lv_obj_delete(unlock_lock_image);
    if (unlock_drawer_image) lv_obj_delete(unlock_drawer_image);
    unlock_lock_image = unlock_drawer_image = nullptr;
}

void destroy_snapshot(lv_draw_buf_t*& pixels)
{
    if (!pixels) return;
    lv_image_cache_drop(pixels);
    lv_draw_buf_destroy(pixels);
    pixels = nullptr;
}

void refresh_snapshot(lv_obj_t* root, lv_draw_buf_t*& pixels)
{
    const auto start = perf::now();
    const bool hidden = lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN);
    const int y = lv_obj_get_style_y(root, LV_PART_MAIN);
    const int scale = lv_obj_get_style_transform_scale_x(root, LV_PART_MAIN);
    const auto opa = lv_obj_get_style_opa(root, LV_PART_MAIN);
    // Ignore the cached page's transition styles while capturing.
    if (y) lv_obj_set_y(root, 0);
    if (scale != 256) {
        lv_obj_set_style_transform_scale_x(root, 256, 0);
        lv_obj_set_style_transform_scale_y(root, 256, 0);
    }
    if (opa != LV_OPA_COVER) lv_obj_set_style_opa(root, LV_OPA_COVER, 0);
    if (hidden) lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
    if (pixels) {
        lv_image_cache_drop(pixels);
        if (lv_snapshot_take_to_draw_buf(root, LV_COLOR_FORMAT_RGB565, pixels) != LV_RESULT_OK)
            destroy_snapshot(pixels);
    }
    if (!pixels) pixels = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
    if (hidden) lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    if (y) lv_obj_set_y(root, y);
    if (scale != 256) {
        lv_obj_set_style_transform_scale_x(root, scale, 0);
        lv_obj_set_style_transform_scale_y(root, scale, 0);
    }
    if (opa != LV_OPA_COVER) lv_obj_set_style_opa(root, opa, 0);
    perf::sample(perf::snapshot_time, perf::now() - start);
}

bool show_unlock_images()
{
    if (!unlock_drawer_pixels || !unlock_lock_pixels) return false;
    unlock_drawer_image = lv_image_create(lv_screen_active());
    lv_image_set_src(unlock_drawer_image, unlock_drawer_pixels);
    lv_obj_set_x(unlock_drawer_image, lv_obj_get_x(drawer));
    lv_image_set_pivot(unlock_drawer_image, 233, 233);
    unlock_lock_image = lv_image_create(lv_screen_active());
    lv_image_set_src(unlock_lock_image, unlock_lock_pixels);
    lv_image_set_pivot(unlock_lock_image, 233, 233);
    lv_obj_add_flag(drawer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lockscreen, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void unlock_frame(void*, int32_t value)
{
    unlock_progress = value;
    if (!lockscreen || !drawer) return;
    if (rendered_progress == value) return;
    rendered_progress = value;
    if (value > 0 && value < 220 && (unlock_lock_image || show_unlock_images())) {
        lv_obj_set_y(unlock_lock_image, -200 * value / 220);
        lv_image_set_scale(unlock_lock_image, 256 - 40 * value / 220);
        lv_obj_set_style_image_opa(unlock_lock_image, 255 - 255 * value / 220, 0);
        lv_obj_set_y(unlock_drawer_image, 120 * (220 - value) / 220);
        lv_image_set_scale(unlock_drawer_image, 230 + 26 * value / 220);
        lv_obj_set_style_image_opa(unlock_drawer_image, 255 * value / 220, 0);
        return;
    }
    release_unlock_images();
    lv_obj_set_flag(drawer, LV_OBJ_FLAG_HIDDEN, value == 0);
    lv_obj_set_flag(lockscreen, LV_OBJ_FLAG_HIDDEN, value == 220);
    lv_obj_set_y(lockscreen, -200 * value / 220);
    lv_obj_set_style_transform_pivot_x(lockscreen, 233, 0);
    lv_obj_set_style_transform_pivot_y(lockscreen, 233, 0);
    lv_obj_set_style_transform_scale_x(lockscreen, 256 - 40 * value / 220, 0);
    lv_obj_set_style_transform_scale_y(lockscreen, 256 - 40 * value / 220, 0);
    lv_obj_set_style_opa(lockscreen, 255 - 255 * value / 220, 0);
    lv_obj_set_y(drawer, 120 * (220 - value) / 220);
    lv_obj_set_style_transform_pivot_x(drawer, 233, 0);
    lv_obj_set_style_transform_pivot_y(drawer, 233, 0);
    lv_obj_set_style_transform_scale_x(drawer, 230 + 26 * value / 220, 0);
    lv_obj_set_style_transform_scale_y(drawer, 230 + 26 * value / 220, 0);
    lv_obj_set_style_opa(drawer, 255 * value / 220, 0);
}

void unlock_finished(lv_anim_t*)
{
    settling = false;
    locked = unlock_progress == 0;
    if (lockscreen) lv_obj_set_flag(lockscreen, LV_OBJ_FLAG_HIDDEN, !locked);
}

void settle_unlock(bool complete)
{
    settling = true;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &unlock_progress);
    lv_anim_set_values(&animation, unlock_progress, complete ? 220 : 0);
    const int duration = complete && unlock_velocity > 0
        ? std::clamp(static_cast<int>((220 - unlock_progress) * unlock_distance / (220.0f * unlock_velocity)), 180, 280) : 280;
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_exec_cb(&animation, unlock_frame);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, unlock_finished);
    lv_anim_start(&animation);
}

void create_dial()
{
    dial = panel(lockscreen, 0, 0, 466, 466);
    for (int hour = 1; hour <= 12; ++hour) {
        const double angle = hour * 3.141592653589793 / 6;
        text(dial, std::to_string(hour).c_str(),
             209 + std::lround(166 * std::sin(angle)),
             211 - std::lround(166 * std::cos(angle)), 48, &toolbox_misans_36);
    }
    for (int tick = 0; tick < 60; ++tick) {
        const double angle = tick * 3.141592653589793 / 30;
        const int inner = tick % 5 == 0 ? 193 : 205;
        tick_points[tick] = {{{static_cast<lv_value_precise_t>(233 + 214 * std::sin(angle)), static_cast<lv_value_precise_t>(233 - 214 * std::cos(angle))},
                              {static_cast<lv_value_precise_t>(233 + inner * std::sin(angle)), static_cast<lv_value_precise_t>(233 - inner * std::cos(angle))}}};
        auto* line = lv_line_create(dial);
        lv_line_set_points(line, tick_points[tick].data(), 2);
        lv_obj_set_style_line_color(line, lv_color_hex(tick % 5 ? 0x4d5664 : 0xe6edf8), 0);
        lv_obj_set_style_line_width(line, tick % 5 ? 2 : 4, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
    }
    dial_day = text(dial, "", 153, 121, 160, &toolbox_misans_20, 0x98a4b7);
    dial_date = text(dial, "", 153, 145, 160, &toolbox_misans_26, 0xb9d6ff);
    battery_arc = lv_arc_create(dial);
    lv_obj_remove_style_all(battery_arc);
    lv_obj_set_pos(battery_arc, 203, 300);
    lv_obj_set_size(battery_arc, 60, 60);
    lv_arc_set_bg_angles(battery_arc, 0, 360);
    lv_arc_set_rotation(battery_arc, 270);
    lv_obj_set_style_arc_width(battery_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(battery_arc, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(battery_arc, lv_color_hex(0x27303c), LV_PART_MAIN);
    lv_obj_set_style_arc_color(battery_arc, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_remove_flag(battery_arc, LV_OBJ_FLAG_CLICKABLE);
    dial_battery = text(dial, "", 203, 316, 60, &toolbox_misans_20, 0xc6dcf9);
    for (int index = 0; index < 3; ++index) {
        hands[index] = lv_line_create(dial);
        lv_obj_set_style_line_color(hands[index], lv_color_hex(index == 2 ? accent : 0xeef4ff), 0);
        lv_obj_set_style_line_width(hands[index], index == 0 ? 12 : index == 1 ? 8 : 2, 0);
        lv_obj_set_style_line_rounded(hands[index], true, 0);
    }
    panel(dial, 225, 225, 16, 16, 0xeef4ff, 8);
    panel(dial, 229, 229, 8, 8, accent, 4);
}

void update_time()
{
    std::time_t now = std::time(nullptr);
    std::tm tm = {};
    localtime_r(&now, &tm);
    const int battery = GetHAL().getBatteryLevel();
    const bool charging = GetHAL().isBatteryCharging(true);
    if (overlay_level) {
        const bool clock = overlay_level == 1;
        ui::battery_level(clock ? clock_battery_icon : control_battery_icon, battery, clock ? 0xc3ddff : 0xf3f6fc);
        lv_obj_set_flag(clock ? clock_charge : control_charge, LV_OBJ_FLAG_HIDDEN, !charging);
        ui::set_text_fmt(clock ? clock_time : control_time, "%02d:%02d", tm.tm_hour, tm.tm_min);
        ui::set_text_fmt(clock ? clock_battery : control_battery, "%d%%", battery);
    }
    if (!lockscreen || !locked || settling || was_pressed || page != Page::None || overlay_level == 2) return;
    auto redraw_if_changed = [](bool changed) { lock_snapshot_dirty |= changed; };
    if (lv_obj_has_flag(lock_charge, LV_OBJ_FLAG_HIDDEN) == charging) {
        lv_obj_set_flag(lock_charge, LV_OBJ_FLAG_HIDDEN, !charging);
        lock_snapshot_dirty = true;
    }
    const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    if (!analog) {
        ui::battery_level(lock_battery, battery, 0xb9c7db);
        redraw_if_changed(ui::set_text_fmt(digital, "%02d:%02d", tm.tm_hour, tm.tm_min));
        redraw_if_changed(ui::set_text_fmt(date_label, "%d月%d日 · 星期%s", tm.tm_mon + 1, tm.tm_mday, weekdays[tm.tm_wday]));
        redraw_if_changed(ui::set_text_fmt(status, "%d%%", battery));
        const auto opa = GetHAL().getWifiStatus().connected ? LV_OPA_COVER : LV_OPA_30;
        if (lv_obj_get_style_opa(lock_wifi, LV_PART_MAIN) != opa) {
            lv_obj_set_style_opa(lock_wifi, opa, 0);
            lock_snapshot_dirty = true;
        }
        return;
    }
    redraw_if_changed(ui::set_text_fmt(dial_day, "周%s", weekdays[tm.tm_wday]));
    redraw_if_changed(ui::set_text_fmt(dial_date, "%d", tm.tm_mday));
    if (lv_arc_get_value(battery_arc) != battery) lv_arc_set_value(battery_arc, battery);
    if (ui::set_text_fmt(dial_battery, "%d", battery)) {
        lv_obj_align_to(dial_battery, battery_arc, LV_ALIGN_CENTER, 0, 0);
        lock_snapshot_dirty = true;
    }
    const int steps[] = {(tm.tm_hour % 12) * 60 + tm.tm_min, tm.tm_min * 60 + tm.tm_sec, tm.tm_sec};
    const double degrees_per_step[] = {.5, .1, 6};
    const int lengths[] = {102, 148, 187};
    for (int i = 0; i < 3; ++i) {
        if (hand_steps[i] == steps[i]) continue;
        hand_steps[i] = steps[i];
        const double angle = steps[i] * degrees_per_step[i] * 3.141592653589793 / 180;
        const int tail = i == 2 ? 30 : 13;
        hand_points[i][0] = {static_cast<lv_value_precise_t>(233 - tail * std::sin(angle)), static_cast<lv_value_precise_t>(233 + tail * std::cos(angle))};
        hand_points[i][1] = {static_cast<lv_value_precise_t>(233 + lengths[i] * std::sin(angle)),
                             static_cast<lv_value_precise_t>(233 - lengths[i] * std::cos(angle))};
        lv_line_set_points(hands[i], hand_points[i].data(), 2);
        lock_snapshot_dirty = true;
    }
}

void tick(lv_timer_t*)
{
    if (lockscreen && locked && !settling && !was_pressed && lv_tick_elaps(shift_tick) >= 300000) {
        shift_tick = lv_tick_get();
        lock_snapshot_dirty = true;
        lv_obj_set_style_translate_x(lockscreen, static_cast<int>(lv_rand(0, 8)) - 4, 0);
        lv_obj_set_style_translate_y(lockscreen, static_cast<int>(lv_rand(0, 8)) - 4, 0);
    }
    if (overlay_level == 1 && !was_pressed && lv_tick_elaps(clock_shown_at) >= 1500) show_overlay(0);
    if (pending_brightness && runtime().post({CommandType::Brightness, desired_brightness, save_brightness})) {
        pending_brightness = save_brightness = false;
    }
    if (pending_volume && runtime().post({CommandType::Volume, desired_volume, save_volume})) {
        pending_volume = save_volume = false;
    }
    if (pending_ble && runtime().post({CommandType::Ble, desired_ble, true})) pending_ble = false;
    if (pending_provision && runtime().post({CommandType::Provision, desired_provision, true})) pending_provision = false;
    if (pending_face && runtime().post({CommandType::Face, analog, true})) pending_face = false;
    if (pending_wifi && runtime().post({CommandType::Wifi, desired_wifi, true})) pending_wifi = false;
    if (drawer_snapshot_dirty && locked && !was_pressed && !settling && page == Page::None && overlay_level < 2) {
        refresh_snapshot(drawer, unlock_drawer_pixels);
        drawer_snapshot_dirty = false;
    }
    auto second = lv_tick_get() / 1000;
    if (second == last_second) return;
    last_second = second;
    update_time();
    if (lockscreen && locked && !settling && !was_pressed && page == Page::None &&
        overlay_level < 2 && lock_snapshot_dirty) {
        refresh_snapshot(lockscreen, unlock_lock_pixels);
        lock_snapshot_dirty = false;
    }
    const bool ble_on = ble_enabled();

    if (ble_status) ui::set_text(ble_status, ble_error() == ESP_ERR_NO_MEM ? "内部内存不足" : ble_error() != ESP_OK ? "BLE 开启失败" : ble_on ? "BLE 已开启" : "BLE 已关闭");
    auto network = GetHAL().getWifiStatus();
    if (network.configMode && !provisioning_visible) {
        show_overlay(0);
        shell_open_settings();
        render_page(Page::Ap);
    }
    provisioning_visible = network.configMode;
    if (page == Page::Wifi || page == Page::Ap) {
        if (page == Page::Ap && !network.configMode) render_page(Page::Wifi);
        if (page == Page::Ap) {
            ui::set_text(network_name, network.apSsid.c_str());
            ui::set_text(network_text, network.apUrl.c_str());
        } else {
            const char* state = network.connected ? "已连接" : network.mode == Hal::WifiMode::Error ? "连接失败" : network.enabled ? "正在连接" : "Wi-Fi 已关闭";
            ui::set_text(network_name, network.ssid.c_str());
            ui::set_text_fmt(network_text, "%s\n%s", state, network.ipAddress.c_str());
            lv_obj_set_state(wifi_switch, LV_STATE_CHECKED, network.enabled);
        }
    }
    if (overlay_level != 2) return;
    for (auto* tile : {wifi_button, ble_button}) {
        const bool on = tile == wifi_button ? GetHAL().isWifiEnabled() : ble_on;
        const auto bg = lv_color_hex(on ? accent : 0x202631);
        auto* content = lv_obj_get_child(tile, 1);
        const auto fg = lv_color_hex(on ? 0x081627 : 0xf3f6fc);
        auto* caption = lv_obj_get_child(content, 1);
        if (!lv_color_eq(lv_obj_get_style_bg_color(tile, LV_PART_MAIN), bg)) lv_obj_set_style_bg_color(tile, bg, 0);
        if (!lv_color_eq(lv_obj_get_style_text_color(caption, LV_PART_MAIN), fg)) {
            lv_obj_set_style_text_color(caption, fg, 0);
            lv_obj_set_style_image_recolor(lv_obj_get_child(content, 0), fg, 0);
        }
    }
    if (lv_indev_get_state(GetHAL().lvTouchpad) == LV_INDEV_STATE_RELEASED) {
        if (!pending_brightness) {
            const int value = GetHAL().getBackLightBrightness();
            if (lv_slider_get_value(brightness) != value) lv_slider_set_value(brightness, value, LV_ANIM_OFF);
            ui::set_text_fmt(brightness_value, "%d", value);
        }
        if (!pending_volume) {
            const int value = GetHAL().getSpeakerVolume();
            if (lv_slider_get_value(volume) != value) lv_slider_set_value(volume, value, LV_ANIM_OFF);
            ui::set_text_fmt(volume_value, "%d", value);
        }
    }
}

void slider_event(lv_event_t* event)
{
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool save = lv_event_get_code(event) == LV_EVENT_RELEASED;
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED && !save) return;
    lv_label_set_text_fmt(static_cast<lv_obj_t*>(lv_event_get_user_data(event)), "%d", static_cast<int>(lv_slider_get_value(slider)));
    if (lv_obj_get_user_data(slider)) {
        desired_brightness = lv_slider_get_value(slider);
        pending_brightness = true;
        save_brightness |= save;
    } else {
        desired_volume = lv_slider_get_value(slider);
        pending_volume = true;
        save_volume |= save;
    }
}

lv_obj_t* slider_card(lv_obj_t* parent, bool is_brightness, int top, lv_obj_t** output)
{
    auto* card = panel(parent, 69, top, 328, 66, 0x202631, 25);
    icon(card, is_brightness ? &toolbox_icon_sun_28 : &toolbox_icon_sound_28, 20, 19);
    *output = text(card, "", 274, 21, 40, &toolbox_misans_18);
    auto* slider = lv_slider_create(card);
    lv_obj_remove_style_all(slider);
    lv_obj_set_pos(slider, 66, 29);
    lv_obj_set_size(slider, 190, 8);
    lv_slider_set_range(slider, is_brightness ? 10 : 0, 100);
    const int value = is_brightness ? GetHAL().getBackLightBrightness() : GetHAL().getSpeakerVolume();
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_label_set_text_fmt(*output, "%d", value);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x4d5664), LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(accent), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 20);
    lv_obj_set_user_data(slider, reinterpret_cast<void*>(static_cast<uintptr_t>(is_brightness ? 1 : 0)));
    lv_obj_add_event_cb(slider, slider_event, LV_EVENT_ALL, *output);
    return slider;
}

void return_to_menu(lv_event_t*) { render_page(Page::Settings); }

void render_page(Page target)
{
    page = target;
    network_name = network_text = wifi_switch = ble_status = nullptr;
    lv_obj_clean(settings);
    lv_obj_remove_flag(settings, LV_OBJ_FLAG_HIDDEN);
    last_second = UINT32_MAX;
    if (target == Page::Settings) {
        ui::heading(settings, "设置", [](lv_event_t*) { shell_close_settings(); });
        auto* rows = panel(settings, 69, ui::content_top, 328, 264);
        lv_obj_add_flag(rows, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(rows, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(rows, LV_SCROLLBAR_MODE_OFF);
        const char* names[] = {"Wi-Fi", "Bluetooth", "屏幕", "声音", "系统信息", "表盘样式", "手机配置", "自动返回 / 关机"};
        const lv_image_dsc_t* symbols[] = {&toolbox_icon_wifi_28, &toolbox_icon_ble_28, &toolbox_icon_sun_28, &toolbox_icon_sound_28, &toolbox_icon_box_28, &toolbox_icon_clock_28, &toolbox_icon_phone_28, &toolbox_icon_power_28};
        const Page pages[] = {Page::Wifi, Page::Ble, Page::Display, Page::Sound, Page::Info, Page::Faces, Page::Phone, Page::Power};
        for (int index = 0; index < 8; ++index) {
            auto* row = button(rows, "", 0, index * 70, 328, 62, [](lv_event_t* event) {
                render_page(static_cast<Page>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))))));
            }, 0x181d25);
            lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(pages[index])));
            auto* content = ui::row(row, 19, 0, 290, 62, 14);
            icon(content, symbols[index], 0, 0);
            auto* caption = ui::row_text(content, names[index], &toolbox_misans_23);
            lv_obj_set_flex_grow(caption, 1);
            lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_LEFT, 0);
            icon(content, &toolbox_icon_right_28, 0, 0, accent);
        }
        return;
    }
    const char* title = target == Page::Wifi ? "Wi-Fi" : target == Page::Ap ? "连接手机" : target == Page::Faces ? "表盘样式" : target == Page::Ble ? "Bluetooth" : target == Page::Display ? "屏幕" : target == Page::Sound ? "声音" : "系统信息";
    ui::heading(settings, target == Page::Power ? "自动返回 / 关机" : target == Page::Phone ? "手机配置" : title, return_to_menu);
    if (target == Page::Power) {
        text(settings, "返回主页时长", 69, 145, 328, &toolbox_misans_23);
        auto* home = lv_dropdown_create(settings);
        lv_obj_remove_style_all(home);
        lv_obj_set_pos(home, 89, 185);
        lv_obj_set_size(home, 288, 50);
        lv_dropdown_set_options(home, "30s\n60s\n90s\n永不");
        lv_dropdown_set_selected(home, home_seconds == 0 ? 3 : home_seconds / 30 - 1);
        text(settings, "自动关机时长", 69, 265, 328, &toolbox_misans_23);
        auto* power = lv_dropdown_create(settings);
        lv_obj_remove_style_all(power);
        lv_obj_set_pos(power, 89, 305);
        lv_obj_set_size(power, 288, 50);
        lv_dropdown_set_options(power, home_seconds ? "5min\n10min\n20min\n30min" : "永不");
        lv_dropdown_set_selected(power, !home_seconds ? 0 : power_seconds == 300 ? 0 : power_seconds == 600 ? 1 : power_seconds == 1200 ? 2 : 3);
        lv_obj_set_state(power, LV_STATE_DISABLED, !home_seconds);
        for (auto* dropdown : {home, power}) {
            lv_obj_set_style_text_font(dropdown, &toolbox_misans_20, 0);
            lv_obj_set_style_text_color(dropdown, lv_color_hex(0xf3f6fc), 0);
            lv_obj_set_style_bg_color(dropdown, lv_color_hex(0x181d25), 0);
            lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dropdown, 22, 0);
            lv_obj_set_style_pad_hor(dropdown, 20, 0);
            lv_obj_set_style_pad_ver(dropdown, 13, 0);
            lv_obj_set_style_bg_color(dropdown, lv_color_hex(0x283950), LV_STATE_PRESSED);
            lv_obj_set_style_opa(dropdown, LV_OPA_40, LV_STATE_DISABLED);
            lv_dropdown_set_symbol(dropdown, LV_SYMBOL_DOWN);
            lv_obj_set_style_text_font(dropdown, LV_FONT_DEFAULT, LV_PART_INDICATOR);
            lv_obj_set_style_text_color(dropdown, lv_color_hex(accent), LV_PART_INDICATOR);
            auto* list = lv_dropdown_get_list(dropdown);
            lv_obj_remove_style_all(list);
            lv_obj_set_style_text_font(list, &toolbox_misans_20, 0);
            lv_obj_set_style_text_color(list, lv_color_hex(0xf3f6fc), 0);
            lv_obj_set_style_bg_color(list, lv_color_hex(0x202631), 0);
            lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(list, 20, 0);
            lv_obj_set_style_pad_all(list, 14, 0);
            lv_obj_set_style_text_line_space(list, 16, 0);
            lv_obj_set_style_max_height(list, 200, 0);
            lv_obj_set_style_bg_color(list, lv_color_hex(0x344861), LV_PART_SELECTED | LV_STATE_CHECKED);
            lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_CHECKED);
            lv_obj_set_style_text_color(list, lv_color_hex(accent), LV_PART_SELECTED | LV_STATE_CHECKED);
            lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
        }
        lv_obj_add_event_cb(home, [](lv_event_t* e) {
            int selected = lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e)));
            int seconds = selected == 3 ? 0 : (selected + 1) * 30;
            if (runtime().post({CommandType::HomeTimeout, seconds, true})) home_seconds = seconds;
            shell_note_activity();
            render_page(Page::Power);
        }, LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_add_event_cb(power, [](lv_event_t* e) {
            const int durations[] = {300, 600, 1200, 1800};
            int seconds = durations[lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e)))];
            if (runtime().post({CommandType::PowerTimeout, seconds, true})) power_seconds = seconds;
        }, LV_EVENT_VALUE_CHANGED, nullptr);
        return;
    }
    if (target == Page::Phone) {
        text(settings, "开启手机配置", 69, 160, 240, &toolbox_misans_23);
        auto* toggle = lv_switch_create(settings);
        lv_obj_set_pos(toggle, 320, 160);
        lv_obj_set_state(toggle, LV_STATE_CHECKED, GetHAL().getWifiStatus().configMode);
        lv_obj_add_event_cb(toggle, [](lv_event_t* e) {
            desired_provision = lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(e)), LV_STATE_CHECKED);
            pending_provision = true;
        }, LV_EVENT_VALUE_CHANGED, nullptr);
        text(settings, "打开配置热点\n已保存的 Wi-Fi 配置会保留", 69, 235, 328, &toolbox_misans_20);
        return;
    }
    if (target == Page::Wifi || target == Page::Ble) {
        auto* row = panel(settings, 69, ui::content_top, 328, 62, 0x181d25, 22);
        auto* content = ui::row(row, 19, 0, 290, 62);
        auto* name = ui::row_text(content, target == Page::Wifi ? "Wi-Fi" : "BLE", &toolbox_misans_23);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
        auto* toggle = lv_switch_create(content);
        lv_obj_set_size(toggle, 56, 32);
        lv_obj_set_style_bg_color(toggle, lv_color_hex(0x7eaff4), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_state(toggle, LV_STATE_CHECKED, target == Page::Wifi ? GetHAL().isWifiEnabled() : ble_enabled());
        lv_obj_add_event_cb(toggle, [](lv_event_t* event) {
            const bool enabled = lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(event)), LV_STATE_CHECKED);
            if (page == Page::Wifi) { desired_wifi = enabled; pending_wifi = true; }
            else { desired_ble = enabled; pending_ble = true; }
        }, LV_EVENT_VALUE_CHANGED, nullptr);
        if (target == Page::Ble) {
            ble_status = text(settings, "", 83, 220, 300, &toolbox_misans_23, 0x98a4b7);
            return;
        }
        wifi_switch = toggle;
        auto* network = panel(settings, 69, 218, 328, 120, 0x181d25, 24);
        network_name = text(network, "", 20, 16, 288, &toolbox_misans_23);
        lv_label_set_long_mode(network_name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(network_name, LV_TEXT_ALIGN_LEFT, 0);
        network_text = text(network, "", 20, 54, 288, &toolbox_misans_20, 0xa8b7ca);
        lv_obj_set_style_text_line_space(network_text, 8, 0);
        lv_label_set_long_mode(network_text, LV_LABEL_LONG_DOT);
        lv_obj_set_height(network_text, 60);
        lv_obj_set_style_text_align(network_text, LV_TEXT_ALIGN_LEFT, 0);
        button(settings, "重新配网", 69, 354, 328, 52, [](lv_event_t*) {
            if (GetHAL().getWifiStatus().configMode) render_page(Page::Ap);
            else { desired_provision = true; pending_provision = true; }
        }, 0x283950);
    } else if (target == Page::Ap) {
        text(settings, "手机连接以下热点", 66, 144, 334, &toolbox_misans_20);
        auto* card = panel(settings, 66, 182, 334, 68, 0x181d25, 24);
        network_name = text(card, "", 0, 20, 334, &toolbox_misans_23);
        text(settings, "在浏览器打开", 66, 269, 334, &toolbox_misans_20);
        network_text = text(settings, "", 66, 301, 334, &toolbox_misans_20, accent);
        button(settings, "退出配网", 151, 346, 164, 52, [](lv_event_t*) {
            desired_provision = false; pending_provision = true;
        }, 0x283950);
    } else if (target == Page::Faces) {
        button(settings, "数字表盘", 69, ui::content_top, 328, 82, [](lv_event_t*) { set_analog(false); pending_face = true; render_page(Page::Settings); }, 0x181d25);
        button(settings, "指针表盘", 69, 234, 328, 82, [](lv_event_t*) { set_analog(true); pending_face = true; render_page(Page::Settings); }, 0x181d25);
    } else if (target == Page::Display || target == Page::Sound) {
        lv_obj_t* value;
        slider_card(settings, target == Page::Display, 200, &value);
    } else {
        text(settings, "ToolBox OS 1.0\nM5Stack StopWatch\nESP32-S3", 69, 150, 328, &toolbox_misans_23);
    }
}
}

void shell_init()
{
    lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(lv_screen_active(), LV_SCROLLBAR_MODE_OFF);
    Settings preferences("toolbox", false);
    analog = preferences.GetInt("analog", 0) != 0;
    home_seconds = preferences.GetInt("home_secs", 60);
    power_seconds = preferences.GetInt("power_secs", 600);
    if (home_seconds != 0 && home_seconds != 30 && home_seconds != 60 && home_seconds != 90) home_seconds = 60;
    if (power_seconds != 300 && power_seconds != 600 && power_seconds != 1200 && power_seconds != 1800) power_seconds = 600;
    shell_note_activity();
    settings = panel(lv_layer_top(), 1, 0, 466, 466);
    lv_obj_add_flag(settings, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN));
    overlay = panel(lv_layer_top(), 1, 0, 466, 466);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(overlay, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN));
    clock_panel = panel(overlay, 113, 0, 240, 132, 0x202936, 44);
    // Square top corners retain the hanging-tab shape of the prototype.
    panel(clock_panel, 0, 0, 240, 44, 0x202936);
    clock_time = text(clock_panel, "", 0, 26, 240, &toolbox_misans_semibold_36, 0xc3ddff);
    auto* clock_row = ui::row(clock_panel, 0, 77, 240, 28, 8);
    clock_battery_icon = ui::battery(clock_row, 0xc3ddff);
    clock_battery = ui::row_text(clock_row, "82%", &toolbox_misans_20, 0xc3ddff);
    clock_charge = ui::charging_icon(clock_row, 0xc3ddff);
    controls = panel(overlay, 0, 0, 466, 466);
    control_time = text(controls, "", 123, 45, 220, &toolbox_misans_24);
    wifi_button = button(controls, "Wi-Fi", 84, 96, 142, 86, [](lv_event_t*) {
        desired_wifi = pending_wifi ? !desired_wifi : !GetHAL().isWifiEnabled(); pending_wifi = true;
    }, accent, &toolbox_misans_18);
    ble_button = button(controls, "BLE", 240, 96, 142, 86, [](lv_event_t*) {
        desired_ble = pending_ble ? !desired_ble : !ble_enabled(); pending_ble = true;
    }, 0x202631, &toolbox_misans_18);
    for (auto* tile : {wifi_button, ble_button}) {
        lv_obj_add_flag(lv_obj_get_child(tile, 0), LV_OBJ_FLAG_HIDDEN);
        auto* content = ui::row(tile, 0, 0, 142, 86, 12);
        icon(content, tile == wifi_button ? &toolbox_icon_wifi_28 : &toolbox_icon_ble_28, 0, 0);
        ui::row_text(content, tile == wifi_button ? "Wi-Fi" : "BLE", &toolbox_misans_18);
        lv_obj_set_style_radius(tile, 28, 0);
    }
    brightness = slider_card(controls, true, 199, &brightness_value);
    volume = slider_card(controls, false, 278, &volume_value);
    auto* bottom = ui::row(controls, 0, 354, 466, 52, 15);
    control_battery_icon = ui::battery(bottom);
    control_battery = ui::row_text(bottom, "82%", &toolbox_misans_22);
    control_charge = ui::charging_icon(bottom);
    icon_button(bottom, &toolbox_icon_settings_28, 0, 0, 58, 52, [](lv_event_t*) {
        show_overlay(0); shell_open_settings();
    });
    lv_timer_create(tick, 20, nullptr);
}

void shell_open_settings(std::function<void()> on_close)
{
    if (page == Page::None) settings_closed = std::move(on_close);
    render_page(Page::Settings);
}

void shell_close_settings()
{
    page = Page::None;
    last_second = UINT32_MAX;
    network_text = ble_status = wifi_switch = nullptr;
    lv_obj_add_flag(settings, LV_OBJ_FLAG_HIDDEN);
    auto callback = std::move(settings_closed);
    settings_closed = {};
    if (callback) callback();
}

void shell_lock()
{
    resume_drawer = false;
    if (!lockscreen) return;
    lv_anim_delete(&unlock_progress, unlock_frame);
    settling = false;
    locked = true;
    const bool changed = rendered_progress != 0;
    lv_obj_remove_flag(lockscreen, LV_OBJ_FLAG_HIDDEN);
    unlock_frame(nullptr, 0);
    if (changed) {
        update_time();
        refresh_snapshot(drawer, unlock_drawer_pixels);
        drawer_snapshot_dirty = false;
        refresh_snapshot(lockscreen, unlock_lock_pixels);
        lock_snapshot_dirty = false;
    }
}

void shell_attach(lv_obj_t* root)
{
    drawer = root;
    drawer_snapshot_dirty = false;
    rendered_progress = -1;
    hand_steps.fill(-1);
    lockscreen = panel(lv_screen_active(), 0, 0, 466, 466, 0);
    digital = text(lockscreen, "", 0, 150, 466, &toolbox_misans_98);
    lv_obj_set_style_text_letter_space(digital, -5, 0);
    date_label = text(lockscreen, "", 33, 280, 400, &toolbox_misans_25, 0xaeb9c9);
    lock_status_row = ui::row(lockscreen, 0, 64, 466, 32, 13);
    lock_wifi = icon(lock_status_row, &toolbox_icon_wifi_28, 0, 0, 0xb9c7db);
    lock_battery = ui::battery(lock_status_row, 0xb9c7db);
    status = ui::row_text(lock_status_row, "82%", &toolbox_misans_23, 0xb9c7db);
    lock_charge = ui::charging_icon(lock_status_row, 0xb9c7db);
    auto* unlock_button = icon_button(lockscreen, &toolbox_icon_up_28, 201, 348, 64, 56, [](lv_event_t*) { settle_unlock(true); });
    lv_obj_set_style_bg_opa(unlock_button, LV_OPA_TRANSP, LV_PART_MAIN);
    create_dial();
    lv_obj_set_style_opa_layered(dial, LV_OPA_70, 0);
    lv_obj_set_style_text_opa(digital, LV_OPA_70, 0);
    set_analog(analog);
    locked = true;
    settling = false;
    unlock_frame(nullptr, 0);
    update_time();
    if (!resume_drawer) {
        refresh_snapshot(drawer, unlock_drawer_pixels);
        refresh_snapshot(lockscreen, unlock_lock_pixels);
        lock_snapshot_dirty = false;
    }
    if (resume_drawer) {
        locked = false;
        unlock_frame(nullptr, 220);
        resume_drawer = false;
    }
}

void shell_detach()
{
    lv_anim_delete(&unlock_progress, unlock_frame);
    release_unlock_images();
    destroy_snapshot(unlock_lock_pixels);
    destroy_snapshot(unlock_drawer_pixels);
    if (lockscreen) lv_obj_delete(lockscreen);
    lockscreen = drawer = nullptr;
    drawer_snapshot_dirty = false;
    settling = false;
}

void shell_drawer_settled(lv_obj_t* root)
{
    if (root == drawer && locked) drawer_snapshot_dirty = true;
}

bool shell_is_locked() { return lockscreen && locked; }
void shell_resume_drawer() { resume_drawer = true; }
void shell_note_activity() { activity_tick = lv_tick_get(); }
uint32_t shell_idle_ms() { return lv_tick_get() - activity_tick.load(); }
int shell_home_timeout() { return home_seconds.load(); }
int shell_power_timeout() { return home_seconds ? power_seconds.load() : 0; }

void shell_cancel_touch()
{
    was_pressed = captured = unlock_dragging = false;
    unlock_velocity = 0;
    if (shell_is_locked() && unlock_progress > 0) settle_unlock(false);
}

bool shell_touch(bool pressed, int x, int y, uint32_t sample_tick, bool draw)
{
    if (pressed) shell_note_activity();
    const uint32_t now = sample_tick == UINT32_MAX ? lv_tick_get() : sample_tick;
    if (!overlay) return false;
    if (overlay_closing) return true;
    if (settling) { was_pressed = pressed; return true; }
    if (pressed && !was_pressed) {
        unlock_dragging = false;
        unlock_velocity = 0;
        motion_sampled = false;
        motion_y = y;
        motion_tick = now;
        start_x = last_x = x;
        start_y = last_y = y;
        captured = (shell_is_locked() && page == Page::None && overlay_level == 0) || y < 64 || overlay_level == 1;
    }
    if (pressed) {
        last_x = x;
        last_y = y;
        if (shell_is_locked() && page == Page::None && overlay_level == 0) {
            if (start_y - y > 6 && start_y - y > std::abs(x - start_x)) unlock_dragging = true;
            if (unlock_dragging) {
                const uint32_t elapsed = (now - motion_tick);
                if (elapsed && motion_y != y) {
                    const float speed = static_cast<float>(motion_y - y) / elapsed;
                    unlock_velocity = motion_sampled ? 0.65f * speed + 0.35f * unlock_velocity : speed;
                    motion_sampled = true;
                    motion_tick = now;
                    motion_y = y;
                }
                const int progress = std::clamp((start_y - y) * 220 / unlock_distance, 0, 220);
                if (draw) unlock_frame(nullptr, progress);
                else unlock_progress = progress;
            } else {
                motion_y = y;
                motion_tick = now;
            }
        }
        if (overlay_level == 2 && start_y - y > 35 && std::abs(x - start_x) < start_y - y) {
            captured = true;
            lv_indev_reset(GetHAL().lvTouchpad, nullptr);
        }
    }
    const bool consume = captured;
    if (!pressed && was_pressed && !settling) {
        const int previous_level = overlay_level;
        const int dy = last_y - start_y;
        if (std::abs(dy) > std::abs(last_x - start_x)) {
            if (dy > 45 && (start_y < 64 || overlay_level == 1)) show_overlay(std::min(2, overlay_level + 1));
            else if (dy < -45 && overlay_level) show_overlay(0);
        }
        if (shell_is_locked() && previous_level == 0 && unlock_progress > 0) {
            if ((now - motion_tick) > 100) unlock_velocity = 0;
            const int distance = start_y - last_y;
            const bool flick = distance >= 16 && unlock_velocity >= 0.30f;
            settle_unlock(unlock_velocity > -0.15f && (distance >= 64 || flick));
        }
    }
    was_pressed = pressed;
    if (!pressed) captured = false;
    return consume;
}
}


