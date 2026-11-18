#include "app_level.h"
#include <esp_timer.h>
#include <system/app_icons.h>
#include <hal/utils/settings/settings.h>
#include <apps/common/audio/audio.h>
#include <hal/hal.h>

namespace toolbox {
namespace {
using A = ExtensionAction;
using P = ExtensionPage;
}

AppLevel::AppLevel() : ToolApp("水平仪", toolbox_app_level) {}

void AppLevel::load() {
    Settings settings("extensions");
    state_.sound = settings.GetBool("sound", true);
    tolerance_ = std::clamp<int>(settings.GetInt("tolerance", 10), 1, 100) / 100.;
    size_t length = sizeof(bias_);
    if (settings.GetBlob("bias", bias_, &length) != ESP_OK || length != sizeof(bias_))
        std::fill_n(bias_, 3, 0.);
    for (double value : bias_)
        if (!std::isfinite(value) || std::abs(value) > 1)
            std::fill_n(bias_, 3, 0.);
}

void AppLevel::save() {
    Settings settings("extensions", true);
    settings.SetBool("sound", state_.sound);
    settings.SetInt("tolerance", static_cast<int>(tolerance_ * 100 + .5));
    settings.SetBlob("bias", bias_, sizeof(bias_));
}

ExtensionViewState AppLevel::view_state() const {
    ExtensionViewState view;
    view.page = state_.page;
    view.confirm = state_.confirm;
    view.frozen = state_.frozen;
    view.sound = state_.sound;
    view.pin = state_.pin;
    view.valid = state_.valid;
    view.angle = state_.angle;
    view.x = state_.x;
    view.y = state_.y;
    view.direction = state_.direction;
    view.error = state_.error;
    view.target = state_.target;
    view.message = state_.message;
    view.tolerance = tolerance_;
    return view;
}

void AppLevel::onOpen() {
    state_ = {};
    calibration_stage_ = calibration_samples_ = 0;
    tone_due_ = 0;
    reading_ = {};
    filter_first_ = true;
    zero_[0] = zero_[1] = target_[0] = target_[1] = 0;
    zero_[2] = target_[2] = 1;
    load();
    open_view(Extension::Level, view_state());
}

bool AppLevel::key_action(AppKey key, int64_t now) {
    if (key == AppKey::A)
        return act({ExtensionAction::Sound}, now);
    if (key == AppKey::B) {
        state_.frozen = !state_.frozen;
        if (state_.frozen) {
            audio::stop();
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
    return false;
}

bool AppLevel::act(const ExtensionEvent& event, int64_t) {
    if (event.action == A::Settings) {
        state_.page = P::Settings;
        return true;
    }
    if (event.action == A::Sound || (event.action == A::Edit && event.value == 0)) {
        state_.sound = !state_.sound;
        if (!state_.sound) {
            audio::stop();
        }
        settings_.changed();
        return true;
    }
    if (event.action == A::Edit && event.value == 1) {
        tolerance_ = tolerance_ < .09 ? .1 : tolerance_ < .19 ? .2 : tolerance_ < .49 ? .5 : .05;
        settings_.changed();
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
    return false;
}

void AppLevel::level_tick(int64_t now) {
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
                settings_.changed();
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
    // Keep the filtered measurements and PIN error at full precision.
    // The view owns the hysteresis and formatting for its visible readouts.
    state_.angle = state_.pin ? reading_.tilt : reading_.error;
    state_.x = reading_.pitch;
    state_.y = reading_.roll;
    state_.direction = reading_.direction;
    state_.error = reading_.error;
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

void AppLevel::onRunning() {
    const int64_t now = esp_timer_get_time();
    if (!handle_input(state_, now)) return;
    settings_.update(now, [this] { save(); });
    if (!view_due(now)) return;
    level_tick(now);
    update_view(view_state());
}

void AppLevel::onClose() {
    settings_.flush([this] { save(); });
    close_view();
    state_ = {};
}
} // namespace toolbox
