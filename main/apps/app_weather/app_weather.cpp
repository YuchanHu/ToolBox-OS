#include "app_weather.h"
#include <esp_timer.h>
#include <ctime>
#include <system/app_icons.h>

namespace toolbox {
namespace {
using A = ExtensionAction;
using P = ExtensionPage;
}

AppWeather::AppWeather() : ToolApp("天气", toolbox_app_weather) {}

ExtensionViewState AppWeather::view_state() const {
    ExtensionViewState view;
    view.page = state_.page;
    view.confirm = state_.confirm;
    view.weather = state_.weather;
    view.day = state_.day;
    return view;
}

void AppWeather::onOpen() {
    state_ = {};
    weather_ = std::make_unique<WeatherService>();
    state_.weather = weather_->snapshot();
    const auto age = std::time(nullptr) - state_.weather.fetched_at;
    if (!state_.weather.fetched_at || !state_.weather.forecast || age < 0 || age >= 7200)
        weather_->refresh(state_.weather.city);
    refresh_due_ = esp_timer_get_time() + 7200000000LL;
    open_view(Extension::Weather, view_state());
}

bool AppWeather::key_action(AppKey key, int64_t) {
    if (key == AppKey::A && state_.day > 0) {
        --state_.day;
        return true;
    }
    if (key == AppKey::B && state_.weather.forecast && state_.day + 1 < static_cast<int>(state_.weather.forecast->days.size())) {
        ++state_.day;
        return true;
    }
    return false;
}

bool AppWeather::act(const ExtensionEvent& event, int64_t) {
    if (event.action == A::City && !state_.weather.busy) {
        if (state_.page == P::City && event.value >= 0 && event.value < static_cast<int>(state_.weather.cities.size())) {
            weather_->refresh(state_.weather.cities[event.value]);
            state_.page = P::Main;
            state_.day = 0;
        } else
            state_.page = P::City;
        return true;
    }
    if (event.action == A::Edit) {
        state_.page = event.value == 1 ? P::Hourly : event.value == 2 ? P::Daily : P::Details;
        return true;
    }
    if (event.action == A::Search && !state_.weather.busy) {
        if (event.text.empty() || event.value == -1) {
            weather_->refresh({});
            state_.day = 0;
            state_.page = P::Main;
            return true;
        }
        weather_->search(event.text);
        return true;
    }
    if (event.action == A::Refresh && !state_.weather.busy) {
        weather_->refresh(state_.weather.city);
        return true;
    }
    return false;
}

void AppWeather::onRunning() {
    const int64_t now = esp_timer_get_time();
    if (!handle_input(state_, now)) return;
    if (!view_due(now)) return;
    state_.weather = weather_->snapshot();
    if (!state_.weather.day(state_.day).valid) state_.day = 0;
    if (now >= refresh_due_) {
        weather_->refresh(state_.weather.city);
        refresh_due_ = now + 7200000000LL;
    }
    update_view(view_state());
}

void AppWeather::onClose() {
    weather_.reset();
    close_view();
    state_ = {};
}
} // namespace toolbox
