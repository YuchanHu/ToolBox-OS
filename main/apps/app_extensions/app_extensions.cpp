#include "app_extensions.h"
#include <apps/common/audio/audio.h>
#include <climits>
#include <cstdio>
#include <cstring>
#include <esp_timer.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <system/shell.h>
#include <system/app_icons.h>

namespace toolbox {
ExtensionApp::ExtensionApp(Extension kind) : kind_(kind) {
    const char *names[] = {"计数器", "倒计时", "番茄钟", "水平仪", "天气"};
    setAppInfo().name = names[static_cast<int>(kind)];
    const lv_image_dsc_t *icons[] = {&toolbox_app_counter, &toolbox_app_timers, &toolbox_app_focus,
                                     &toolbox_app_level, &toolbox_app_weather};
    setAppInfo().icon = const_cast<lv_image_dsc_t *>(icons[static_cast<int>(kind)]);
}
void ExtensionApp::load() {
    Settings settings("extensions");
    if (kind_ == Extension::Counter)
        state_.count = static_cast<uint32_t>(std::max<int>(0, settings.GetInt("count", 0)));
    if (kind_ == Extension::Timers)
        set_duration(std::clamp<int>(settings.GetInt("duration", 600), 1, 359999));
    if (kind_ == Extension::Focus) {
        state_.values = {std::clamp<int>(settings.GetInt("focus", 25), 1, 180),
                         std::clamp<int>(settings.GetInt("short", 5), 1, 60),
                         std::clamp<int>(settings.GetInt("long", 15), 1, 120)};
        state_.completed = std::max<int>(0, settings.GetInt("completed", 0));
        set_duration(state_.values[0] * 60);
    }
    if (kind_ == Extension::Level) {
        state_.sound = settings.GetBool("sound", true);
        tolerance_ = std::clamp<int>(settings.GetInt("tolerance", 10), 1, 100) / 100.;
        size_t length = sizeof(bias_);
        if (settings.GetBlob("bias", bias_, &length) != ESP_OK || length != sizeof(bias_))
            std::fill_n(bias_, 3, 0.);
        for (double value : bias_)
            if (!std::isfinite(value) || std::abs(value) > 1)
                std::fill_n(bias_, 3, 0.);
    }
}
void ExtensionApp::save() {
    Settings settings("extensions", true);
    if (kind_ == Extension::Counter)
        settings.SetInt("count", static_cast<int32_t>(state_.count));
    if (kind_ == Extension::Timers)
        settings.SetInt("duration", clock_.duration / 1000000);
    if (kind_ == Extension::Focus) {
        settings.SetInt("focus", state_.values[0]);
        settings.SetInt("short", state_.values[1]);
        settings.SetInt("long", state_.values[2]);
        settings.SetInt("completed", state_.completed);
    }
    if (kind_ == Extension::Level) {
        settings.SetBool("sound", state_.sound);
        settings.SetInt("tolerance", static_cast<int>(tolerance_ * 100 + .5));
        settings.SetBlob("bias", bias_, sizeof(bias_));
    }
    dirty_ = false;
}
void ExtensionApp::set_duration(int seconds) {
    clock_.reset(static_cast<int64_t>(seconds) * 1000000);
}
void ExtensionApp::onOpen() {
    state_ = {};
    state_.kind = kind_;
    home_ = combo_ = dirty_ = false;
    updated_ = tone_due_ = alert_until_ = 0;
    calibration_stage_ = calibration_samples_ = 0;
    reading_ = {};
    filter_first_ = true;
    std::fill_n(bias_, 3, 0.);
    zero_[0] = zero_[1] = target_[0] = target_[1] = 0;
    zero_[2] = target_[2] = 1;
    clock_ = {};
    load();
    if (kind_ == Extension::Timers)
        state_.page = ExtensionPage::Presets;
    if (kind_ == Extension::Weather) {
        weather_ = std::make_unique<WeatherService>();
        state_.weather = weather_->snapshot();
        if (!state_.weather.city.id.empty())
            weather_->refresh(state_.weather.city);
        refresh_due_ = esp_timer_get_time() + 1800000000LL;
    }
    GetHAL().applicationButtonFeedback = true;
    LvglLockGuard lock;
    view_ = std::make_unique<ExtensionView>(kind_);
    view_->update(state_, GetHAL().millis());
}
AppKey ExtensionApp::key() {
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
bool ExtensionApp::key_action(AppKey key, int64_t now) {
    if (key == AppKey::None)
        return false;
    if (key == AppKey::Home) {
        home_ = true;
        return true;
    }
    if (state_.confirm) {
        if (kind_ == Extension::Focus && key == AppKey::A)
            return act({ExtensionAction::Confirm}, now);
        return false;
    }
    if (state_.page != ExtensionPage::Main)
        return false;
    if (kind_ == Extension::Counter) {
        if (key == AppKey::B && state_.count < INT32_MAX) {
            ++state_.count;
            dirty_ = true;
            return true;
        }
        if (key == AppKey::A && state_.count) {
            --state_.count;
            dirty_ = true;
            return true;
        }
        if (key == AppKey::HoldA) {
            state_.confirm = true;
            return true;
        }
    }
    if (kind_ == Extension::Timers) {
        if (key == AppKey::A)
            return act({ExtensionAction::Secondary}, now);
        if (key == AppKey::B)
            return act({ExtensionAction::Primary}, now);
    }
    if (kind_ == Extension::Focus) {
        if (key == AppKey::B && !state_.rest && clock_.state == Countdown::State::Ready)
            return act({ExtensionAction::Primary}, now);
        if (key == AppKey::A && !state_.rest && clock_.state == Countdown::State::Running) {
            state_.confirm = true;
            return true;
        }
    }
    if (kind_ == Extension::Weather) {
        if (key == AppKey::A && state_.day > 0) {
            --state_.day;
            return true;
        }
        if (key == AppKey::B && state_.day < 1) {
            ++state_.day;
            return true;
        }
    }
    if (kind_ == Extension::Level) {
        if (key == AppKey::A)
            return act({ExtensionAction::Sound}, now);
        if (key == AppKey::B) {
            state_.frozen = !state_.frozen;
            if (state_.frozen) {
                std::vector<int16_t> silence;
                GetHAL().audioPlay(silence, true);
            }
            tone_due_ = 0;
            return true;
        }
        if (key == AppKey::HoldA && reading_.valid) {
            zero_[0] = reading_.x;
            zero_[1] = reading_.y;
            zero_[2] = reading_.z;
            return true;
        }
        if (key == AppKey::HoldB && reading_.valid) {
            state_.pin = !state_.pin;
            if (state_.pin) {
                target_[0] = reading_.x;
                target_[1] = reading_.y;
                target_[2] = reading_.z;
                state_.target = reading_.tilt;
            }
            return true;
        }
    }
    return false;
}
bool ExtensionApp::act(const ExtensionEvent &event, int64_t now) {
    using A = ExtensionAction;
    using P = ExtensionPage;
    if (event.action == A::None)
        return false;
    if (event.action == A::Input)
        return true;
    if (event.action == A::Back) {
        if (state_.page == P::Main || state_.page == P::Presets)
            home_ = true;
        else
            state_.page = P::Main;
        calibration_samples_ = 0;
        return true;
    }
    if (event.action == A::Cancel) {
        state_.confirm = false;
        return true;
    }
    if (kind_ == Extension::Counter && event.action == A::Confirm && state_.confirm) {
        state_.count = 0;
        state_.confirm = false;
        dirty_ = true;
        return true;
    }
    if (event.action == A::Settings) {
        state_.page = P::Settings;
        return true;
    }
    if (kind_ == Extension::Timers || kind_ == Extension::Focus) {
        if (event.action == A::Primary) {
            alert_until_ = 0;
            if (kind_ == Extension::Focus && clock_.state == Countdown::State::Running) {
                state_.confirm = true;
                return true;
            }
            clock_.toggle(now);
            return true;
        }
        if (event.action == A::Secondary) {
            clock_.reset(clock_.duration);
            alert_until_ = 0;
            return true;
        }
        if (event.action == A::Confirm && state_.confirm) {
            state_.confirm = false;
            clock_.reset(clock_.duration);
            return true;
        }
        if (kind_ == Extension::Timers) {
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
                dirty_ = true;
                if (event.action == A::Confirm)
                    state_.page = P::Main;
                return true;
            }
        } else {
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
                dirty_ = true;
                if (clock_.state == Countdown::State::Ready)
                    set_duration(state_.values[state_.rest ? (state_.round == 0 ? 2 : 1) : 0] * 60);
                return true;
            }
        }
    }
    if (kind_ == Extension::Level) {
        if (event.action == A::Sound || (event.action == A::Edit && event.value == 0)) {
            state_.sound = !state_.sound;
            if (!state_.sound) {
                std::vector<int16_t> silence;
                GetHAL().audioPlay(silence, true);
            }
            dirty_ = true;
            return true;
        }
        if (event.action == A::Edit && event.value == 1) {
            tolerance_ = tolerance_ < .09 ? .1 : tolerance_ < .19 ? .2 : tolerance_ < .49 ? .5 : .05;
            dirty_ = true;
            return true;
        }
        if (event.action == A::Edit && event.value == 2) {
            zero_[0] = zero_[1] = 0;
            zero_[2] = 1;
            return true;
        }
        if (event.action == A::Edit && event.value == 3) {
            state_.page = P::Calibration;
            state_.message = "采集第一次后原地旋转";
            return true;
        }
        if (event.action == A::Calibrate && !calibration_samples_) {
            state_.frozen = false;
            calibration_samples_ = 1;
            std::fill_n(calibration_sum_, 3, 0.);
            state_.message = "正在采集，保持静止";
            return true;
        }
    }
    if (kind_ == Extension::Weather) {
        if (event.action == A::City && !state_.weather.busy) {
            if (state_.page == P::City && event.value < static_cast<int>(state_.weather.cities.size())) {
                weather_->refresh(state_.weather.cities[event.value]);
                state_.page = P::Main;
                state_.day = 0;
            } else
                state_.page = P::City;
            return true;
        }
        if (event.action == A::Edit) {
            state_.page = P::Details;
            return true;
        }
        if (event.action == A::Search && !event.text.empty() && !state_.weather.busy) {
            weather_->search(event.text);
            return true;
        }
        if (event.action == A::Refresh && !state_.weather.city.id.empty() && !state_.weather.busy) {
            weather_->refresh(state_.weather.city);
            return true;
        }
    }
    return false;
}

void ExtensionApp::level_tick(int64_t now) {
    if (state_.frozen)
        return;
    GetHAL().updateImuData();
    const auto data = GetHAL().getImuData();
    // HAL already swaps sensor X/Y. Screen +X is right and +Y is down.
    const double raw[] = {data.accelX, data.accelY, data.accelZ};
    if (calibration_samples_) {
        const double magnitude = std::sqrt(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]);
        if (magnitude < .8 || magnitude > 1.2) {
            calibration_samples_ = 0;
            state_.message = "姿态不稳定，请重新采集";
            return;
        }
        for (int i = 0; i < 3; ++i)
            calibration_sum_[i] += raw[i];
        if (++calibration_samples_ > 64) {
            for (double &v : calibration_sum_)
                v /= 64.;
            if (!calibration_stage_) {
                std::copy_n(calibration_sum_, 3, calibration_first_);
                calibration_stage_ = 1;
                state_.message = "原地旋转 180° 后再次采集";
            } else {
                for (int i = 0; i < 2; ++i)
                    bias_[i] = (calibration_first_[i] + calibration_sum_[i]) / 2.;
                bias_[2] = (calibration_first_[2] + calibration_sum_[2]) / 2. - 1;
                dirty_ = true;
                filter_first_ = true;
                calibration_stage_ = 0;
                state_.message = "校准完成";
            }
            calibration_samples_ = 0;
        }
    }
    reading_.sample(raw[0], raw[1], raw[2], bias_, state_.pin ? target_ : zero_, filter_first_);
    filter_first_ = !reading_.valid;
    state_.valid = reading_.valid;
    if (std::abs(state_.angle - reading_.tilt) > .08) state_.angle = reading_.tilt;
    if (std::abs(state_.x - reading_.pitch) > .08) state_.x = reading_.pitch;
    if (std::abs(state_.y - reading_.roll) > .08) state_.y = reading_.roll;
    state_.direction = reading_.direction;
    state_.error = reading_.error;
    state_.tolerance = tolerance_;
    if (!state_.pin) {
        if (std::abs(state_.angle - reading_.error) > .08) state_.angle = reading_.error;
    }
    if (!reading_.valid) {
        state_.message = "等待稳定读数";
        return;
    }
    if (state_.sound && reading_.error <= 10 && now >= tone_due_ && state_.page == ExtensionPage::Main) {
        const bool on = reading_.error <= tolerance_;
        const double side =
            (reading_.x - (state_.pin ? target_[0] : zero_[0])) + (reading_.y - (state_.pin ? target_[1] : zero_[1]));
        audio::play_tone(on ? 1760 : side < 0 ? 1100 : 1400, on ? .10f : .025f, .25f);
        tone_due_ = now + (on ? 180000 : static_cast<int64_t>(120000 + reading_.error * 65000));
    }
}
void ExtensionApp::remind(int64_t now) {
    alert_until_ = now + 3000000;
    tone_due_ = now;
}
void ExtensionApp::onRunning() {
    const int64_t now = esp_timer_get_time();
    if (GetHAL().getBackLightBrightness() == 0) {
        close();
        return;
    }
    const auto input = key();
    ExtensionEvent event;
    {
        LvglLockGuard lock;
        event = view_->take_event();
    }
    const bool accepted = input == AppKey::None ? act(event, now) : key_action(input, now);
    if (accepted && !home_ && event.action != ExtensionAction::Back) {
        GetHAL().buttonFeedback(input != AppKey::A && input != AppKey::HoldA);
        tone_due_ = now + 150000;
    }
    if (home_) {
        close();
        return;
    }
    if (clock_.tick(now)) {
        state_.confirm = false;
        remind(now);
        if (kind_ == Extension::Focus) {
            if (!state_.rest) {
                if (state_.completed < INT_MAX)
                    ++state_.completed;
                state_.round = (state_.round + 1) % 4;
                dirty_ = true;
                state_.rest = true;
                set_duration(state_.values[state_.round == 0 ? 2 : 1] * 60);
            } else {
                state_.rest = false;
                set_duration(state_.values[0] * 60);
            }
        }
    }
    if (alert_until_ > now && now >= tone_due_) {
        audio::play_tone(1760, .07f, .4f);
        GetHAL().vibrate(70, 70);
        tone_due_ = now + 600000;
    }
    if (dirty_ && !save_due_)
        save_due_ = now + 1000000;
    if (dirty_ && now >= save_due_) {
        save();
        save_due_ = 0;
    }
    if (now - updated_ < 33000)
        return;
    updated_ = now;
    if (kind_ == Extension::Level)
        level_tick(now);
    if (kind_ == Extension::Weather) {
        state_.weather = weather_->snapshot();
        if (now >= refresh_due_ && !state_.weather.city.id.empty()) {
            weather_->refresh(state_.weather.city);
            refresh_due_ = now + 1800000000LL;
        }
    }
    if (kind_ == Extension::Timers || kind_ == Extension::Focus) {
        const int64_t seconds = (clock_.remaining + 999999) / 1000000;
        char text[32];
        if (seconds >= 3600)
            snprintf(text, sizeof(text), "%02lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60, seconds % 60);
        else
            snprintf(text, sizeof(text), "%02lld:%02lld", seconds / 60, seconds % 60);
        state_.time = text;
        state_.timer = clock_.state;
        state_.progress = clock_.duration ? static_cast<double>(clock_.remaining) / clock_.duration : 0;
        state_.detail = kind_ == Extension::Focus
                            ? (state_.rest ? (state_.round == 0 ? "长休息" : "短休息")
                                           : "专注 · 第 " + std::to_string(state_.round + 1) + " / 4 轮")
                            : std::to_string(clock_.duration / 60000000) + " 分钟";
    }
    LvglLockGuard lock;
    view_->update(state_, GetHAL().millis());
}
void ExtensionApp::onClose() {
    clock_.reset(clock_.duration);
    alert_until_ = tone_due_ = 0;
    calibration_samples_ = 0;
    weather_.reset();
    if (dirty_)
        save();
    save_due_ = 0;
    std::vector<int16_t> silence;
    GetHAL().audioPlay(silence, true);
    GetHAL().stopVibrate();
    GetHAL().applicationButtonFeedback = false;
    LvglLockGuard lock;
    view_.reset();
}
} // namespace toolbox
