#include "camera_ui.h"
#include <cstdio>
#include <ctime>
#include <utility>
#include <hal/hal.h>

namespace toolbox {
using ui::set_text;

CameraPage::CameraPage()
{
    root_ = ui::round_page(lv_screen_active());

    ring_ = lv_arc_create(root_);
    lv_obj_remove_style_all(ring_);
    lv_obj_set_pos(ring_, 4, 4);
    lv_obj_set_size(ring_, 458, 458);
    lv_arc_set_bg_angles(ring_, 0, 360);
    lv_obj_set_style_arc_opa(ring_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring_, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_remove_flag(ring_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    auto* header = ui::row(root_, 0, 45, 466, 32, 8);
    clock_ = ui::row_text(header, "", &toolbox_misans_20, 0x9daebd);
    battery_ = ui::battery(header, 0xbdcbd8);
    battery_percent_ = ui::row_text(header, "", &toolbox_misans_20, 0xcbd6e0);

    title_ = ui::text(root_, "", 107, 91, 252, &toolbox_misans_26);
    back_ = ui::icon_button(root_, &toolbox_camera_back, 61, 87, 42, 44, [](lv_event_t*) {}, 0x131d25);
    lv_obj_set_style_image_recolor(lv_obj_get_child(back_, -1), lv_color_hex(0xb8c8d5), 0);
    message_ = ui::text(root_, "", 45, 134, 376, &toolbox_misans_26);
    detail_ = ui::text(root_, "", 55, 174, 356, &toolbox_misans_20, 0x9daebd);
    hint_ = ui::text(root_, "", 68, 333, 330, &toolbox_misans_20, 0x9daebd);
    for (auto* label : {message_, detail_, hint_}) lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);

    devices_ = ui::menu_list(root_, 220, 126);
    ui::cache_scroll_rows(devices_);

    modes_ = ui::panel(root_, 90, 221, 286, 150);
    auto* photo = ui::button(modes_, "拍照遥控", 0, 0, 286, 65, [](lv_event_t*) {}, 0x151e27, &toolbox_misans_26);
    auto* movie = ui::button(modes_, "录像遥控", 0, 77, 286, 65, [](lv_event_t*) {}, 0x151e27, &toolbox_misans_26);
    ui::icon(photo, &toolbox_camera_photo_small, 19, 20, 0xafc1da);
    ui::icon(movie, &toolbox_camera_movie, 19, 20, 0xafc1da);
    ui::text(photo, "KEYA", 203, 21, 52, &toolbox_misans_18, 0x9daebd);
    ui::text(movie, "KEYB", 203, 21, 52, &toolbox_misans_18, 0x9daebd);
    for (auto* button : {photo, movie}) ui::icon(button, &toolbox_camera_right, 255, 22, 0x9daebd);

    symbol_ = ui::icon(root_, &toolbox_camera_photo, 204, 225);

    time_ = ui::text(root_, "", 23, 215, 420, &toolbox_misans_65);
    rescan_ = ui::button(root_, "重新扫描", 146, 363, 174, 49, [](lv_event_t*) {}, 0x16222b, &toolbox_misans_20);
    const std::pair<lv_obj_t*, ActionType> buttons[] = {
        {back_, ActionType::Back}, {photo, ActionType::Photo}, {movie, ActionType::Movie}, {rescan_, ActionType::Rescan},
    };
    for (const auto& button : buttons) {
        lv_obj_set_user_data(button.first, reinterpret_cast<void*>(button.second));
        lv_obj_add_event_cb(button.first, event, LV_EVENT_CLICKED, this);
    }
}

CameraPage::~CameraPage() { lv_obj_delete(root_); }

void CameraPage::event(lv_event_t* event)
{
    auto* self = static_cast<CameraPage*>(lv_event_get_user_data(event));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (self->has_action() || lv_obj_has_flag(self->back_, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(target, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_state(target, LV_STATE_DISABLED)) return;
    const uintptr_t code = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));
    if (code >= 100) {
        const size_t index = code - 100;
        if (index >= self->displayed_devices_.size()) return;
        self->pending_ = {ActionType::Select, self->displayed_devices_[index]};
    } else {
        self->pending_.type = static_cast<ActionType>(code);
    }
    self->action_ready_.store(true);
}

CameraPage::Action CameraPage::take_action()
{
    Action result = std::move(pending_);
    pending_ = {};
    action_ready_.store(false);
    return result;
}

bool CameraPage::devices_changed(const std::vector<camera::Device>& devices) const
{
    if (devices.size() != displayed_devices_.size()) return true;
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& old = displayed_devices_[i];
        const auto& next = devices[i];
        if (old.address != next.address || old.address_type != next.address_type || old.name != next.name) return true;
    }
    return false;
}

void CameraPage::rebuild_devices(const camera::Status& status)
{
    lv_obj_clean(devices_);
    displayed_devices_ = status.devices;
    for (size_t index = 0; index < displayed_devices_.size(); ++index) {
        auto* button = ui::menu_row(devices_, displayed_devices_[index].name.c_str(), 0, index * ui::menu_pitch).object;
        lv_obj_set_user_data(button, reinterpret_cast<void*>(index + 100));
        lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED, this);
    }
}

bool CameraPage::is_recording(const camera::Status& status)
{
    return status.phase == camera::Phase::Ready && status.recording_known && status.recording;
}

CameraPage::Scene CameraPage::scene_for(const camera::Status& status, Screen screen, bool stopping)
{
    using camera::Phase;
    switch (status.phase) {
    case Phase::Off: return Scene::BluetoothOff;
    case Phase::Error: return Scene::Error;
    case Phase::Connecting: return Scene::Connecting;
    case Phase::Pairing: return Scene::Pairing;
    case Phase::Starting:
    case Phase::Scanning:
        if (!status.name.empty()) return Scene::Reconnect;
        return status.devices.empty() ? Scene::Search : Scene::Devices;
    case Phase::Ready:
        break;
    }
    if (is_recording(status)) return stopping && status.busy ? Scene::MovieStop : Scene::Recording;
    if (screen == Screen::Modes) return Scene::Modes;
    if (screen == Screen::Photo) {
        if (status.busy) return Scene::PhotoBusy;
        return status.message == "快门指令已发送" ? Scene::PhotoSent : Scene::PhotoReady;
    }
    if (status.busy) return stopping ? Scene::MovieFinishing : Scene::MovieStart;
    // A ready camera accepts KEYB even before its first recording notification.
    return Scene::MovieReady;
}

uint32_t CameraPage::elapsed_seconds(const camera::Status& status, uint32_t now)
{
    constexpr uint32_t recording_delay_ms = 500;
    const uint32_t elapsed_ms = now - status.recording_since;
    return elapsed_ms >= recording_delay_ms ? (elapsed_ms - recording_delay_ms) / 1000 : 0;
}

bool CameraPage::needs_update(const camera::Status& status, Screen screen, uint32_t now, bool stopping) const
{
    const auto scene = scene_for(status, screen, stopping);
    return !initialized_ || scene != scene_ || now - header_at_ >= 1000 ||
        (is_recording(status) && elapsed_seconds(status, now) != previous_second_) ||
        status.name != previous_name_ ||
        (scene == Scene::Modes && status.busy != previous_busy_) ||
        (scene == Scene::Error && status.message != previous_message_) ||
        (scene == Scene::Devices && devices_changed(status.devices));
}

void CameraPage::update(const camera::Status& status, Screen screen, uint32_t now, bool stopping)
{
    struct Theme { const char* label; uint32_t ring, ink; };
    constexpr Theme themes[] = {
        {"正在搜索相机",     0x448e9d, 0x91d2db}, // Search
        {"发现附近相机",     0x448e9d, 0x91d2db},
        {"正在连接",         0x416cc7, 0xa0bcf7},
        {"等待配对确认",     0x925ac0, 0xcfaaef},
        {"相机已连接",       0x697b95, 0xafc1da},
        {"待拍照",           0x697b95, 0xafc1da},
        {"正在发送快门",     0xb58432, 0xe7be7e},
        {"快门指令已发送",   0x697b95, 0xafc1da},
        {"待录制",           0x3c965e, 0x91d4aa},
        {"等待开录确认",     0xb58432, 0xe7be7e},
        {"相机已停录",       0xb58432, 0xe7be7e},
        {"正在录制",         0xbe3b49, 0xf6959c},
        {"等待停录确认",     0xbe3b49, 0xf6959c},
        {"正在重新连接",     0x416cc7, 0xa0bcf7},
        {"相机连接异常",     0xad5b37, 0xedaf8c},
        {"蓝牙已关闭",       0x83734a, 0xd0bd87}, // BluetoothOff
    };
    static_assert(sizeof(themes) / sizeof(themes[0]) == static_cast<size_t>(Scene::BluetoothOff) + 1);

    const auto scene = scene_for(status, screen, stopping);
    const bool changed = !initialized_ || scene_ != scene;
    const bool recording = is_recording(status);
    const bool photo_page = scene >= Scene::PhotoReady && scene <= Scene::PhotoSent;
    const bool movie_page = scene >= Scene::MovieReady && scene <= Scene::MovieStop;
    if (changed) {
        const auto& theme = themes[static_cast<size_t>(scene)];
        if (!initialized_ || themes[static_cast<size_t>(scene_)].ring != theme.ring)
            lv_obj_set_style_arc_color(ring_, lv_color_hex(theme.ring), LV_PART_MAIN);
        const int ring_width = recording ? 14 : 8;
        if (lv_obj_get_style_arc_width(ring_, LV_PART_MAIN) != ring_width)
            lv_obj_set_style_arc_width(ring_, ring_width, LV_PART_MAIN);
        lv_obj_set_style_text_color(message_, lv_color_hex(theme.ink), 0);
        set_text(title_, photo_page ? "拍照遥控" : movie_page ? "录像遥控" : "相机遥控");
        set_text(message_, theme.label);
        lv_obj_set_flag(back_, LV_OBJ_FLAG_HIDDEN, recording);
        lv_obj_set_flag(devices_, LV_OBJ_FLAG_HIDDEN, scene != Scene::Devices);
        lv_obj_set_flag(modes_, LV_OBJ_FLAG_HIDDEN, scene != Scene::Modes);
        lv_obj_set_flag(time_, LV_OBJ_FLAG_HIDDEN, !movie_page);
        lv_obj_set_style_text_color(time_, lv_color_hex(recording ? 0xf2f5f8 : 0x9daebd), 0);
        lv_obj_set_flag(rescan_, LV_OBJ_FLAG_HIDDEN,
                       scene != Scene::Search && scene != Scene::Devices && scene != Scene::Reconnect &&
                       scene != Scene::Error && scene != Scene::BluetoothOff);

        const lv_image_dsc_t* symbol = nullptr;
        const char* hint = "";
        switch (scene) {
        case Scene::Search: symbol = &toolbox_camera_search; hint = "在相机上开启蓝牙遥控配对"; break;
        case Scene::Connecting: symbol = &toolbox_camera_connect; hint = "正在建立蓝牙连接"; break;
        case Scene::Pairing: symbol = &toolbox_camera_bluetooth; hint = "请在相机上确认蓝牙配对"; break;
        case Scene::PhotoReady: symbol = &toolbox_camera_photo; hint = "按 KEYB 拍照"; break;
        case Scene::PhotoBusy: symbol = &toolbox_camera_photo; hint = "请稍候"; break;
        case Scene::PhotoSent: symbol = &toolbox_camera_check; hint = "按 KEYB 再拍一张"; break;
        case Scene::MovieReady: hint = "按 KEYB 开始录像"; break;
        case Scene::MovieStart: hint = "正在发送录像指令"; break;
        case Scene::MovieFinishing: hint = "正在完成停止指令"; break;
        case Scene::Recording: hint = "按 KEYB 停止录像"; break;
        case Scene::MovieStop: hint = "相机确认停录后返回待机"; break;
        case Scene::Reconnect: symbol = &toolbox_camera_connect; hint = "靠近相机以恢复连接"; break;
        case Scene::Error: symbol = &toolbox_camera_warning; hint = "可重新扫描相机"; break;
        case Scene::BluetoothOff: symbol = &toolbox_camera_bluetooth; hint = "重新扫描会开启蓝牙"; break;
        default: break;
        }
        if (symbol) {
            lv_image_set_src(symbol_, symbol);
            lv_obj_set_style_image_recolor(symbol_, lv_color_hex(theme.ink), 0);
            const int center_y = scene == Scene::Search ? 230 : 252;
            lv_obj_set_pos(symbol_, (466 - symbol->header.w) / 2, center_y - symbol->header.h / 2);
        }
        lv_obj_set_flag(symbol_, LV_OBJ_FLAG_HIDDEN, symbol == nullptr);
        lv_obj_set_y(hint_, movie_page ? 344 : (photo_page ? 332 : 315));
        set_text(hint_, hint);
    }
    if (changed || status.name != previous_name_ || (scene == Scene::Error && status.message != previous_message_)) {
        const char* detail = status.name.c_str();
        if (scene == Scene::Devices) detail = "点选要连接的相机";
        if (scene == Scene::Error) detail = status.message.c_str();
        set_text(detail_, detail);
        previous_name_ = status.name;
        previous_message_ = status.message;
    }
    if (scene == Scene::Devices && devices_changed(status.devices)) rebuild_devices(status);
    if (scene == Scene::Modes && (changed || status.busy != previous_busy_)) {
        for (uint32_t i = 0; i < lv_obj_get_child_count(modes_); ++i)
            lv_obj_set_state(lv_obj_get_child(modes_, i), LV_STATE_DISABLED, status.busy);
    }

    if (movie_page) {
        const uint32_t second = recording ? elapsed_seconds(status, now) : 0;
        if (changed || previous_second_ != second) {
            ui::set_text_fmt(time_, "%02lu:%02lu:%02lu", static_cast<unsigned long>(second / 3600),
                             static_cast<unsigned long>(second / 60 % 60), static_cast<unsigned long>(second % 60));
            previous_second_ = second;
        }
    }

    if (!initialized_ || now - header_at_ >= 1000) {
        const std::time_t wall_time = std::time(nullptr);
        std::tm tm = {};
        localtime_r(&wall_time, &tm);
        ui::set_text_fmt(clock_, "%02d:%02d", tm.tm_hour, tm.tm_min);
        const auto power = GetHAL().getBatteryStatus();
        ui::battery_level(battery_, power.percent, 0xbdcbd8);
        ui::battery_external_power(battery_, power.external_power);
        ui::set_text_fmt(battery_percent_, "%u%%", static_cast<unsigned>(power.percent));
        header_at_ = now;
    }
    scene_ = scene;
    previous_busy_ = status.busy;
    initialized_ = true;
}
}
