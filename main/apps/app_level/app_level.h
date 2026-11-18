#pragma once
#include <apps/common/ui/tool_app.h>
#include "level_reading.h"

namespace toolbox {
class AppLevel final : public ToolApp {
  public:
    AppLevel();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

  private:
    struct State : ToolPage {
        bool frozen = false, sound = true, pin = false, valid = false;
        double angle = 0, x = 0, y = 0, direction = 0, error = 180, target = 0;
        std::string message;
    } state_;
    ExtensionViewState view_state() const;
    bool act(const ExtensionEvent& event, int64_t now) override;
    bool key_action(AppKey key, int64_t now) override;
    DeferredSettings settings_;
    void load();
    void save();
    LevelReading reading_;
    bool filter_first_ = true;
    int64_t tone_due_ = 0;
    int calibration_stage_ = 0, calibration_samples_ = 0;
    double bias_[3]{}, target_[3]{0, 0, 1}, zero_[3]{0, 0, 1};
    double calibration_sum_[3]{}, calibration_first_[3]{};
    double tolerance_ = .1;
    void level_tick(int64_t now);
    void on_back() override { calibration_samples_ = 0; }
};
} // namespace toolbox
