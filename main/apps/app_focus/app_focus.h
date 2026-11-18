#pragma once
#include <apps/common/ui/tool_app.h>
#include <apps/common/countdown/countdown_alert.h>

namespace toolbox {
class AppFocus final : public ToolApp {
  public:
    AppFocus();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

  private:
    struct State : ToolPage {
        std::array<int, 3> values{25, 5, 15};
        int edit = 0, round = 0, completed = 0;
        bool rest = false;
    } state_;
    ExtensionViewState view_state() const;
    bool act(const ExtensionEvent& event, int64_t now) override;
    bool key_action(AppKey key, int64_t now) override;
    DeferredSettings settings_;
    void load();
    void save();
    Countdown clock_;
    CountdownAlert alert_;
    void set_duration(int seconds);
};
} // namespace toolbox
