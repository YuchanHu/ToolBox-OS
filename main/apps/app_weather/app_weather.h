#pragma once
#include <apps/common/ui/tool_app.h>

namespace toolbox {
class AppWeather final : public ToolApp {
  public:
    AppWeather();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

  private:
    struct State : ToolPage {
        WeatherResult weather;
        int day = 0;
    } state_;
    ExtensionViewState view_state() const;
    bool act(const ExtensionEvent& event, int64_t now) override;
    bool key_action(AppKey key, int64_t now) override;
    std::unique_ptr<WeatherService> weather_;
    int64_t refresh_due_ = 0;
};
} // namespace toolbox
