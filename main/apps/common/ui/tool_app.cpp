#include "tool_app.h"
#include <apps/common/audio/audio.h>
#include <hal/hal.h>

namespace toolbox {
ToolApp::ToolApp(const char* name, const lv_image_dsc_t& icon)
{
    setAppInfo().name = name;
    setAppInfo().icon = const_cast<lv_image_dsc_t*>(&icon);
}

void ToolApp::open_view(Extension kind, const ExtensionViewState& state)
{
    combo_ = false;
    updated_ = 0;
    GetHAL().applicationButtonFeedback = true;
    LvglLockGuard lock;
    view_ = std::make_unique<ExtensionView>(kind);
    view_->update(state, GetHAL().millis());
}

AppKey ToolApp::key() {
    auto &hal = GetHAL();
    hal.updateButtonStates();
    if (hal.btnA.isPressed() && hal.btnB.isPressed())
        combo_ = true;
    if (combo_) {
        if (hal.btnA.isHolding() && hal.btnB.isHolding())
            return AppKey::Home;
        if (hal.btnA.isReleased() && hal.btnB.isReleased())
            combo_ = false;
        return AppKey::None;
    }
    // Wait until release to arbitrate single-key holds against the home chord.
    if (hal.btnA.wasReleasedAfterHold())
        return AppKey::HoldA;
    if (hal.btnB.wasReleasedAfterHold())
        return AppKey::HoldB;
    if (hal.btnA.wasClicked())
        return AppKey::A;
    if (hal.btnB.wasClicked())
        return AppKey::B;
    return AppKey::None;
}

bool ToolApp::handle_input(ToolPage& page, int64_t now)
{
    if (GetHAL().getBackLightBrightness() == 0) {
        close();
        return false;
    }
    const auto input = key();
    ExtensionEvent event;
    {
        LvglLockGuard lock;
        event = view_->take_event();
    }
    using A = ExtensionAction;
    using P = ExtensionPage;
    if (input == AppKey::Home || (input == AppKey::None && event.action == A::Back &&
                                  (page.page == P::Main || page.page == P::Presets))) {
        close();
        return false;
    }
    bool accepted = false;
    if (input != AppKey::None) {
        accepted = page.page == P::Main && key_action(input, now);
    } else if (event.action == A::Back) {
        page.page = P::Main;
        on_back();
    } else if (event.action == A::Cancel) {
        page.confirm = false;
        accepted = true;
    } else if (event.action != A::None) {
        accepted = event.action == A::Input || act(event, now);
    }
    if (accepted) GetHAL().buttonFeedback(input != AppKey::A && input != AppKey::HoldA);
    return true;
}

bool ToolApp::view_due(int64_t now)
{
    constexpr int64_t interval = 33000;
    const auto elapsed = now - updated_;
    if (elapsed < interval) return false;
    updated_ = now - elapsed % interval;
    return true;
}

void ToolApp::update_view(const ExtensionViewState& state)
{
    LvglLockGuard lock;
    view_->update(state, GetHAL().millis());
}

void ToolApp::close_view()
{
    audio::stop();
    GetHAL().stopVibrate();
    GetHAL().applicationButtonFeedback = false;
    LvglLockGuard lock;
    view_.reset();
}
} // namespace toolbox
