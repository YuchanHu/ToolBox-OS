#include "app_focus.h"
#include <esp_timer.h>
#include <system/app_icons.h>
#include <hal/utils/settings/settings.h>
#include <climits>

namespace toolbox {
namespace {
using A = ExtensionAction;
using P = ExtensionPage;
}

AppFocus::AppFocus() : ToolApp("番茄钟", toolbox_app_focus) {}

void AppFocus::load() {
    Settings settings("extensions");
    state_.values = {std::clamp<int>(settings.GetInt("focus", 25), 1, 180),
                     std::clamp<int>(settings.GetInt("short", 5), 1, 60),
                     std::clamp<int>(settings.GetInt("long", 15), 1, 120)};
    state_.completed = std::max<int>(0, settings.GetInt("completed", 0));
    set_duration(state_.values[0] * 60);
}

void AppFocus::save() {
    Settings settings("extensions", true);
    settings.SetInt("focus", state_.values[0]);
    settings.SetInt("short", state_.values[1]);
    settings.SetInt("long", state_.values[2]);
    settings.SetInt("completed", state_.completed);
}

void AppFocus::set_duration(int seconds) {
    clock_.reset(static_cast<int64_t>(seconds) * 1000000);
}

ExtensionViewState AppFocus::view_state() const {
    ExtensionViewState view;
    view.page = state_.page;
    view.confirm = state_.confirm;
    view.values = state_.values;
    view.edit = state_.edit;
    view.round = state_.round;
    view.completed = state_.completed;
    view.rest = state_.rest;
    view.time = countdown_text(clock_);
    view.timer = clock_.state;
    view.progress = static_cast<double>(clock_.remaining) / clock_.duration;
    view.detail = state_.rest ? (state_.round == 0 ? "长休息" : "短休息")
                              : "专注 · 第 " + std::to_string(state_.round + 1) + " / 4 轮";
    return view;
}

void AppFocus::onOpen() {
    state_ = {};
    load();
    open_view(Extension::Focus, view_state());
}

bool AppFocus::key_action(AppKey key, int64_t now) {
    if (state_.confirm) return key == AppKey::A && act({A::Confirm}, now);
    if (key == AppKey::B && !state_.rest && clock_.state == Countdown::State::Ready)
        return act({ExtensionAction::Primary}, now);
    if (key == AppKey::A && !state_.rest && clock_.state == Countdown::State::Running) {
        state_.confirm = true;
        return true;
    }
    return false;
}

bool AppFocus::act(const ExtensionEvent& event, int64_t now) {
    if (event.action == A::Settings) {
        state_.page = P::Settings;
        return true;
    }
    if (event.action == A::Primary) {
        alert_.reset();
        if (clock_.state == Countdown::State::Running) {
            state_.confirm = true;
            return true;
        }
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
    if (event.action == A::Edit) {
        if (event.value == 3)
            state_.page = P::Stats;
        else
            state_.edit = event.value;
        return true;
    }
    if (event.action == A::Plus || event.action == A::Minus) {
        const int max[] = {180, 60, 120};
        auto &v = state_.values[state_.edit];
        v = std::clamp(v + (event.action == A::Plus ? 1 : -1), 1, max[state_.edit]);
        settings_.changed();
        if (clock_.state == Countdown::State::Ready)
            set_duration(state_.values[state_.rest ? (state_.round == 0 ? 2 : 1) : 0] * 60);
        return true;
    }
    return false;
}

void AppFocus::onRunning() {
    const int64_t now = esp_timer_get_time();
    if (!handle_input(state_, now)) return;
    if (clock_.tick(now)) {
        state_.confirm = false;
        alert_.start(now);
        if (!state_.rest) {
            if (state_.completed < INT_MAX)
                ++state_.completed;
            state_.round = (state_.round + 1) % 4;
            settings_.changed();
            state_.rest = true;
            set_duration(state_.values[state_.round == 0 ? 2 : 1] * 60);
        } else {
            state_.rest = false;
            set_duration(state_.values[0] * 60);
        }
    }
    alert_.update(now);
    settings_.update(now, [this] { save(); });
    if (!view_due(now)) return;
    update_view(view_state());
}

void AppFocus::onClose() {
    alert_.reset();
    clock_.reset(clock_.duration);
    settings_.flush([this] { save(); });
    close_view();
    state_ = {};
}
} // namespace toolbox
