#include "app_counter.h"
#include <esp_timer.h>
#include <system/app_icons.h>
#include <hal/utils/settings/settings.h>
#include <climits>

namespace toolbox {
namespace {
using A = ExtensionAction;
using P = ExtensionPage;
}

AppCounter::AppCounter() : ToolApp("计数器", toolbox_app_counter) {}

void AppCounter::load() {
    Settings settings("extensions");
    state_.count = static_cast<uint32_t>(std::max<int>(0, settings.GetInt("count", 0)));
}

void AppCounter::save() {
    Settings settings("extensions", true);
    settings.SetInt("count", static_cast<int32_t>(state_.count));
}

ExtensionViewState AppCounter::view_state() const {
    ExtensionViewState view;
    view.page = state_.page;
    view.confirm = state_.confirm;
    view.count = state_.count;
    return view;
}

void AppCounter::onOpen() {
    state_ = {};
    load();
    open_view(Extension::Counter, view_state());
}

bool AppCounter::key_action(AppKey key, int64_t) {
    if (state_.confirm) return false;
    if (key == AppKey::B && state_.count < INT32_MAX) {
        ++state_.count;
        settings_.changed();
        return true;
    }
    if (key == AppKey::A && state_.count) {
        --state_.count;
        settings_.changed();
        return true;
    }
    if (key == AppKey::HoldA) {
        state_.confirm = true;
        return true;
    }
    return false;
}

bool AppCounter::act(const ExtensionEvent& event, int64_t) {
    if (event.action == A::Confirm && state_.confirm) {
        state_.count = 0;
        state_.confirm = false;
        settings_.changed();
        return true;
    }
    return false;
}

void AppCounter::onRunning() {
    const int64_t now = esp_timer_get_time();
    if (!handle_input(state_, now)) return;
    settings_.update(now, [this] { save(); });
    if (!view_due(now)) return;
    update_view(view_state());
}

void AppCounter::onClose() {
    settings_.flush([this] { save(); });
    close_view();
    state_ = {};
}
} // namespace toolbox
