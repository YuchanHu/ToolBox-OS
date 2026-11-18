#include "app_timers.h"
#include <esp_timer.h>
#include <system/app_icons.h>
#include <hal/utils/settings/settings.h>

namespace toolbox {
namespace {
using A = ExtensionAction;
using P = ExtensionPage;
}

AppTimers::AppTimers() : ToolApp("倒计时", toolbox_app_timers) {}

void AppTimers::load() {
    Settings settings("extensions");
    set_duration(std::clamp<int>(settings.GetInt("duration", 600), 1, 359999));
}

void AppTimers::save() {
    Settings settings("extensions", true);
    settings.SetInt("duration", clock_.duration / 1000000);
}

void AppTimers::set_duration(int seconds) {
    clock_.reset(static_cast<int64_t>(seconds) * 1000000);
}

ExtensionViewState AppTimers::view_state() const {
    ExtensionViewState view;
    view.page = state_.page;
    view.confirm = state_.confirm;
    view.values = state_.values;
    view.time = countdown_text(clock_);
    view.timer = clock_.state;
    view.progress = static_cast<double>(clock_.remaining) / clock_.duration;
    view.detail = std::to_string(clock_.duration / 60000000) + " 分钟";
    return view;
}

void AppTimers::onOpen() {
    state_ = {};
    state_.page = P::Presets;
    load();
    open_view(Extension::Timers, view_state());
}

bool AppTimers::key_action(AppKey key, int64_t now) {
    if (key == AppKey::A)
        return act({ExtensionAction::Secondary}, now);
    if (key == AppKey::B)
        return act({ExtensionAction::Primary}, now);
    return false;
}

bool AppTimers::act(const ExtensionEvent& event, int64_t now) {
    if (event.action == A::Primary) {
        alert_.reset();
        clock_.toggle(now);
        return true;
    }
    if (event.action == A::Secondary) {
        clock_.reset(clock_.duration);
        alert_.reset();
        return true;
    }
    if (event.action == A::Confirm && state_.confirm) {
        state_.confirm = false;
        clock_.reset(clock_.duration);
        return true;
    }
    if (event.action == A::Preset) {
        if (event.value)
            set_duration(event.value * 60);
        state_.page = P::Main;
        return true;
    }
    if (event.action == A::Custom) {
        state_.page = P::Custom;
        const int s = clock_.duration / 1000000;
        state_.values = {s / 3600, s / 60 % 60, s % 60};
        return true;
    }
    if (event.action == A::Plus || event.action == A::Minus) {
        auto &v = state_.values[event.value];
        v = std::clamp(v + (event.action == A::Plus ? 1 : -1), 0, event.value == 0 ? 99 : 59);
        return true;
    }
    if (event.action == A::Save || event.action == A::Confirm) {
        const int seconds = state_.values[0] * 3600 + state_.values[1] * 60 + state_.values[2];
        if (!seconds)
            return false;
        set_duration(seconds);
        settings_.changed();
        if (event.action == A::Confirm)
            state_.page = P::Main;
        return true;
    }
    return false;
}

void AppTimers::onRunning() {
    const int64_t now = esp_timer_get_time();
    if (!handle_input(state_, now)) return;
    if (clock_.tick(now)) {
        state_.confirm = false;
        alert_.start(now);
    }
    alert_.update(now);
    settings_.update(now, [this] { save(); });
    if (!view_due(now)) return;
    update_view(view_state());
}

void AppTimers::onClose() {
    alert_.reset();
    clock_.reset(clock_.duration);
    settings_.flush([this] { save(); });
    close_view();
    state_ = {};
}
} // namespace toolbox
