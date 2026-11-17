#pragma once
#include <memory>
#include <mooncake.h>
#include <system/extension_ui.h>

namespace toolbox {
class ExtensionApp : public mooncake::AppAbility {
  public:
    explicit ExtensionApp(Extension kind);
    void onCreate() override {}
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

  private:
    Extension kind_;
    ExtensionViewState state_;
    std::unique_ptr<ExtensionView> view_;
    std::unique_ptr<WeatherService> weather_;
    Countdown clock_;
    bool home_ = false, combo_ = false, dirty_ = false, filter_first_ = true;
    int64_t updated_ = 0, save_due_ = 0, tone_due_ = 0, alert_until_ = 0, refresh_due_ = 0;
    int calibration_stage_ = 0, calibration_samples_ = 0;
    double bias_[3]{}, target_[3]{0, 0, 1}, zero_[3]{0, 0, 1}, calibration_sum_[3]{}, calibration_first_[3]{};
    double tolerance_ = .1;
    LevelReading reading_;
    AppKey key();
    bool act(const ExtensionEvent &event, int64_t now);
    bool key_action(AppKey key, int64_t now);
    void level_tick(int64_t now);
    void save();
    void load();
    void remind(int64_t now);
    void set_duration(int seconds);
};
} // namespace toolbox
