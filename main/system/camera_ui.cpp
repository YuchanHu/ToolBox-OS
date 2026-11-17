#include "camera_ui.h"
#include <cstdio>
#include <utility>
#include <hal/hal.h>

namespace toolbox {
namespace {
void set_text(lv_obj_t* label, const char* value)
{
    if (std::string(lv_label_get_text(label)) != value) lv_label_set_text(label, value);
}
}

CameraPage::CameraPage()
{
    root_ = ui::panel(lv_screen_active(), 1, 0, 466, 466);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root_, [](lv_event_t* event) {
        static_cast<CameraPage*>(lv_event_get_user_data(event))->reveal_requested_ = true;
    }, LV_EVENT_PRESSED, this);
    ui::heading(root_, "相机遥控", [](lv_event_t*) {});
    title_ = lv_obj_get_child(root_, 0);
    auto* back = lv_obj_get_child(root_, 1);
    lv_obj_set_user_data(back, reinterpret_cast<void*>(ActionType::Back));
    lv_obj_add_event_cb(back, event, LV_EVENT_CLICKED, this);
    message_ = ui::text(root_, "", 65, ui::content_top, 336, &toolbox_misans_23, ui::accent);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_MODE_DOTS);
    detail_ = ui::text(root_, "", 65, 178, 336, &toolbox_misans_20, 0x98a4b7);
    lv_label_set_long_mode(detail_, LV_LABEL_LONG_MODE_DOTS);
    devices_ = ui::panel(root_, 69, 216, 328, 160);
    lv_obj_add_flag(devices_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(devices_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(devices_, LV_SCROLLBAR_MODE_OFF);
    modes_ = ui::panel(root_, 69, 216, 328, 156);
    auto* photo = ui::button(modes_, "拍照遥控", 0, 0, 328, 66, [](lv_event_t*) {}, 0x181d25);
    auto* movie = ui::button(modes_, "录像遥控", 0, 82, 328, 66, [](lv_event_t*) {}, 0x181d25);
    for (auto* button : {photo, movie}) ui::icon(button, &toolbox_icon_right_28, 280, 19, 0x98a4b7);

    photo_page_ = ui::panel(root_, 69, 210, 328, 212);
    ui::icon(photo_page_, &toolbox_icon_camera_78, 125, 0, ui::accent);
    photo_action_ = ui::button(photo_page_, "拍照", 74, 98, 180, 64, [](lv_event_t*) {}, ui::accent, &toolbox_misans_26);
    ui::text(photo_page_, "KEYB 拍照", 0, 179, 328, &toolbox_misans_20, 0x98a4b7);

    movie_page_ = ui::panel(root_, 69, 210, 328, 212);
    time_ = ui::text(movie_page_, "", 0, 22, 328, &toolbox_misans_28, 0xffffff);
    lv_label_set_long_mode(time_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    ui::text(movie_page_, "KEYB 录像启停", 0, 110, 328, &toolbox_misans_20, 0xffffff);
    rescan_ = ui::button(root_, "重新扫描", 151, 398, 164, 48, [](lv_event_t*) {}, 0x202631, &toolbox_misans_20);
    const std::pair<lv_obj_t*, ActionType> buttons[] = {{photo, ActionType::Photo}, {movie, ActionType::Movie},
        {photo_action_, ActionType::Trigger}, {rescan_, ActionType::Rescan}};
    for (const auto& button : buttons) {
        lv_obj_set_user_data(button.first, reinterpret_cast<void*>(button.second));
        lv_obj_add_event_cb(button.first, event, LV_EVENT_CLICKED, this);
    }
}

CameraPage::~CameraPage() { lv_obj_delete(root_); }

void CameraPage::event(lv_event_t* event)
{
    auto* self = static_cast<CameraPage*>(lv_event_get_user_data(event));
    self->reveal_requested_ = true;
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (self->pending_.type != ActionType::None) return;
    const uintptr_t code = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));
    if (code >= 100) {
        const size_t index = code - 100;
        if (index >= self->previous_.devices.size()) return;
        self->pending_ = {ActionType::Select, self->previous_.devices[index]};
    } else self->pending_.type = static_cast<ActionType>(code);
}

CameraPage::Action CameraPage::take_action()
{
    Action result = std::move(pending_);
    pending_ = {};
    return result;
}

void CameraPage::rebuild_devices(const camera::Status& status)
{
    lv_obj_clean(devices_);
    for (size_t index = 0; index < status.devices.size(); ++index) {
        auto* button = ui::button(devices_, "", 0, index * 70, 328, 62, [](lv_event_t*) {}, 0x181d25);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(index + 100));
        lv_obj_add_event_cb(button, event, LV_EVENT_CLICKED, this);
        auto* content = ui::row(button, 19, 0, 290, 62, 12);
        ui::icon(content, &toolbox_icon_camera_28, 0, 0, ui::accent);
        auto* name = ui::row_text(content, status.devices[index].name.c_str(), &toolbox_misans_20);
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
        ui::icon(content, &toolbox_icon_right_28, 0, 0, 0x98a4b7);
    }
}

void CameraPage::update(const camera::Status& status, Screen screen, uint32_t now)
{
    const bool pressed = GetHAL().lvTouchpad && lv_indev_get_state(GetHAL().lvTouchpad) == LV_INDEV_STATE_PRESSED;
    if (!initialized_ || previous_screen_ != screen) controls_visible_ = false;
    if (reveal_requested_ || (pressed && !touch_down_)) {
        controls_visible_ = true;
        controls_at_ = now;
    }
    reveal_requested_ = false;
    if (pressed && controls_visible_) controls_at_ = now;
    if (now - controls_at_ >= 2000) controls_visible_ = false;
    if (screen == Screen::Movie) controls_visible_ = false;
    touch_down_ = pressed;
    previous_screen_ = screen;
    lv_obj_set_flag(lv_obj_get_child(root_, 1), LV_OBJ_FLAG_HIDDEN, !controls_visible_);
    lv_obj_set_flag(photo_action_, LV_OBJ_FLAG_HIDDEN, !controls_visible_);
    const bool selecting = status.name.empty();
    const uint32_t background = screen == Screen::Movie
        ? (status.phase == camera::Phase::Ready
            ? (status.recording_known && status.recording ? 0x8c2025 : 0x17613b)
            : (status.phase == camera::Phase::Connecting || status.phase == camera::Phase::Pairing ? 0x174c94 : 0x886b12))
        : 0x000000;
    lv_obj_set_style_bg_color(root_, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(movie_page_, LV_OPA_TRANSP, 0);
    bool devices_changed = status.devices.size() != previous_.devices.size();
    for (size_t index = 0; !devices_changed && index < status.devices.size(); ++index) {
        devices_changed = status.devices[index].address != previous_.devices[index].address ||
            status.devices[index].name != previous_.devices[index].name;
    }
    if (!initialized_ || devices_changed) rebuild_devices(status);
    const bool failed = status.phase == camera::Phase::Error || status.phase == camera::Phase::Off;
    lv_obj_set_flag(devices_, LV_OBJ_FLAG_HIDDEN, !selecting);
    lv_obj_set_flag(modes_, LV_OBJ_FLAG_HIDDEN, !controls_visible_ || selecting || failed || screen != Screen::Modes);
    lv_obj_set_flag(photo_page_, LV_OBJ_FLAG_HIDDEN, selecting || failed || screen != Screen::Photo);
    lv_obj_set_flag(movie_page_, LV_OBJ_FLAG_HIDDEN, selecting || failed || screen != Screen::Movie);
    set_text(title_, selecting || screen == Screen::Modes ? "相机遥控" : screen == Screen::Photo ? "拍照遥控" : "录像遥控");
    lv_obj_set_flag(rescan_, LV_OBJ_FLAG_HIDDEN, !controls_visible_ || (!selecting && !failed));
    for (auto* label : {title_, message_, detail_, time_, lv_obj_get_child(photo_action_, 0)})
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    set_text(message_, selecting && !failed ? "选择索尼相机" : status.message.c_str());
    if (selecting) set_text(detail_, status.devices.empty() ? "请在相机上开启蓝牙遥控配对" : "点击相机进行连接");
    else set_text(detail_, status.name.c_str());
    const bool ready = status.phase == camera::Phase::Ready && !status.busy;
    for (auto* action : {photo_action_}) {
        lv_obj_set_state(action, LV_STATE_DISABLED, !ready);
        lv_obj_set_style_opa(action, ready ? LV_OPA_COVER : LV_OPA_50, 0);
    }
    for (uint32_t index = 0; index < lv_obj_get_child_count(modes_); ++index)
        lv_obj_set_state(lv_obj_get_child(modes_, index), LV_STATE_DISABLED, status.busy);
    const uint32_t second = status.recording_known && status.recording ? (now - status.recording_since) / 1000 : 0;
    const lv_font_t* time_font = status.recording_known && status.recording
        ? &toolbox_misans_65 : &toolbox_misans_28;
    if (lv_obj_get_style_text_font(time_, LV_PART_MAIN) != time_font)
        lv_obj_set_style_text_font(time_, time_font, 0);
    if (!initialized_ || previous_second_ != second ||
        previous_.recording != status.recording || previous_.recording_known != status.recording_known) {
        char elapsed[24] = {};
        if (status.recording_known && status.recording)
            snprintf(elapsed, sizeof(elapsed), "%02lu:%02lu:%02lu", static_cast<unsigned long>(second / 3600),
                static_cast<unsigned long>(second / 60 % 60), static_cast<unsigned long>(second % 60));
        else snprintf(elapsed, sizeof(elapsed), "%s", status.recording_known ? "未在录像" : "等待相机状态");
        set_text(time_, elapsed);
    }
    previous_second_ = second;
    previous_ = status;
    initialized_ = true;
}
}
