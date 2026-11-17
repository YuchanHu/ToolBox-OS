#include "app_camera.h"
#include <hal/hal.h>
#include <system/app_icons.h>

using namespace toolbox;
AppCamera::AppCamera()
{
    setAppInfo().name = "相机遥控";
    setAppInfo().icon = (void*)&toolbox_app_camera;
}

void AppCamera::onOpen()
{
    screen_ = CameraPage::Screen::Modes;
    camera::start();
    const auto status = camera::status();
    LvglLockGuard lock;
    view_ = std::make_unique<CameraPage>();
    view_->update(status, screen_, GetHAL().millis());
    updated_at_ = GetHAL().millis();
}

void AppCamera::onRunning()
{
    GetHAL().applicationButtonFeedback = screen_ == CameraPage::Screen::Movie;
    GetHAL().updateButtonStates();
    if (keys_.update(false) == input::KeyEvent::GoHome) { close(); return; }
    if (screen_ == CameraPage::Screen::Movie && (GetHAL().btnA.wasPressed() || GetHAL().btnB.wasPressed()))
        GetHAL().buttonFeedback(GetHAL().btnB.wasPressed(), false);
    CameraPage::Action action;
    {
        LvglLockGuard lock;
        action = view_->take_action();
    }
    const bool was_control_page = screen_ != CameraPage::Screen::Modes;
    if (action.type == CameraPage::ActionType::Back) {
        if (!was_control_page) { close(); return; }
        screen_ = CameraPage::Screen::Modes;
    }
    const auto status = camera::status();
    if (!status.busy) {
        if (!was_control_page && !status.name.empty()) {
            if (GetHAL().btnA.wasClicked()) screen_ = CameraPage::Screen::Photo;
            if (GetHAL().btnB.wasClicked()) screen_ = CameraPage::Screen::Movie;
        }
        if (action.type == CameraPage::ActionType::Photo) screen_ = CameraPage::Screen::Photo;
        if (action.type == CameraPage::ActionType::Movie) screen_ = CameraPage::Screen::Movie;
    }
    if (action.type == CameraPage::ActionType::Rescan) {
        screen_ = CameraPage::Screen::Modes;
        camera::start();
    }
    if (action.type == CameraPage::ActionType::Select) camera::connect(action.device);
    // Only an already open control page accepts KEYB. Navigation never fires a command.
    const bool key_trigger = GetHAL().btnB.wasClicked() && action.type == CameraPage::ActionType::None;
    if (was_control_page && screen_ != CameraPage::Screen::Modes &&
        ((screen_ == CameraPage::Screen::Photo && action.type == CameraPage::ActionType::Trigger) || key_trigger)) {
        if (action.type == CameraPage::ActionType::Trigger) GetHAL().buttonFeedback(true, screen_ != CameraPage::Screen::Movie);
        camera::trigger(screen_ == CameraPage::Screen::Photo ? camera::Mode::Photo : camera::Mode::Movie);
    }
    const uint32_t now = GetHAL().millis();
    if (now - updated_at_ < 50 && action.type == CameraPage::ActionType::None && !GetHAL().btnB.wasClicked()) return;
    const auto fresh = camera::status();
    LvglLockGuard lock;
    view_->update(fresh, screen_, now);
    updated_at_ = now;
}

void AppCamera::onClose()
{
    GetHAL().applicationButtonFeedback = false;
    camera::stop();
    LvglLockGuard lock;
    view_.reset();
}
