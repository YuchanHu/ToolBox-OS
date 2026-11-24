#include "app_camera.h"
#include <hal/hal.h>
#include <system/app_icons.h>
#include <system/shell.h>
#include <system/touch_input.h>

using namespace toolbox;
AppCamera::AppCamera()
{
    setAppInfo().name = "相机遥控";
    setAppInfo().icon = (void*)&toolbox_app_camera;
}

void AppCamera::onOpen()
{
    screen_ = CameraPage::Screen::Modes;
    stopping_movie_ = false;
    camera::start();
    const auto status = camera::status();
    polled_at_ = GetHAL().millis();
    recording_activity_at_ = polled_at_;
    LvglLockGuard lock;
    view_ = std::make_unique<CameraPage>();
    view_->update(status, screen_, polled_at_);
}

void AppCamera::onRunning()
{
    auto& hal = GetHAL();
    hal.applicationButtonFeedback = screen_ == CameraPage::Screen::Movie;
    hal.updateButtonStates();
    if (keys_.update(false) == input::KeyEvent::GoHome) { close(); return; }
    if (screen_ == CameraPage::Screen::Movie && (hal.btnA.wasPressed() || hal.btnB.wasPressed()))
        hal.buttonFeedback(hal.btnB.wasPressed(), false);

    const auto previous_screen = screen_;
    const bool shutter = screen_ == CameraPage::Screen::Photo && hal.btnB.wasPressed() && !hal.btnA.isPressed();
    const bool movie_toggle = screen_ == CameraPage::Screen::Movie && hal.btnB.wasClicked();
    const bool mode_key = screen_ == CameraPage::Screen::Modes && (hal.btnA.wasClicked() || hal.btnB.wasClicked());
    const bool touch_action = view_->has_action();
    constexpr uint32_t status_poll_ms = 100;
    if (!shutter && !movie_toggle && !mode_key && !touch_action && hal.millis() - polled_at_ < status_poll_ms) return;

    auto status = camera::status();
    CameraPage::Action action;
    if (touch_action) {
        LvglLockGuard lock;
        action = view_->take_action();
    }
    // Camera notifications take priority over a queued tap or a mode change.
    if (CameraPage::is_recording(status)) {
        screen_ = CameraPage::Screen::Movie;
        action = {};
    }
    if (action.type == CameraPage::ActionType::Back) {
        if (screen_ == CameraPage::Screen::Modes) { close(); return; }
        screen_ = CameraPage::Screen::Modes;
    }

    const bool can_command = status.phase == camera::Phase::Ready && !status.busy;
    if (can_command && previous_screen == CameraPage::Screen::Modes && screen_ == previous_screen) {
        if (action.type == CameraPage::ActionType::Photo || (mode_key && hal.btnA.wasClicked()))
            screen_ = CameraPage::Screen::Photo;
        if (action.type == CameraPage::ActionType::Movie || (mode_key && hal.btnB.wasClicked()))
            screen_ = CameraPage::Screen::Movie;
    }

    if (!status.busy) stopping_movie_ = false;
    if (action.type == CameraPage::ActionType::Rescan) {
        screen_ = CameraPage::Screen::Modes;
        camera::start();
        status = camera::status();
    } else if (action.type == CameraPage::ActionType::Select) {
        camera::connect(action.device);
        status = camera::status();
    } else if (can_command && screen_ == previous_screen && action.type == CameraPage::ActionType::None) {
        if (shutter) {
            camera::trigger(camera::Mode::Photo);
            status = camera::status();
        } else if (movie_toggle) {
            stopping_movie_ = CameraPage::is_recording(status);
            camera::trigger(camera::Mode::Movie);
            status = camera::status();
        }
    }

    const bool recording = CameraPage::is_recording(status);
    if (recording) screen_ = CameraPage::Screen::Movie;
    const uint32_t now = hal.millis();
    polled_at_ = now;
    const bool touch_changed = recording != touch_input_blocked();
    if (recording && (touch_changed || now - recording_activity_at_ >= 1000)) {
        shell_note_activity();
        recording_activity_at_ = now;
    }
    if (!touch_changed && !view_->needs_update(status, screen_, now, stopping_movie_)) return;
    LvglLockGuard lock;
    if (touch_changed) set_touch_input_blocked(hal.lvTouchpad, recording);
    view_->update(status, screen_, now, stopping_movie_);
}

void AppCamera::onClose()
{
    auto& hal = GetHAL();
    hal.applicationButtonFeedback = false;
    camera::stop();
    LvglLockGuard lock;
    set_touch_input_blocked(hal.lvTouchpad, false);
    view_.reset();
}
