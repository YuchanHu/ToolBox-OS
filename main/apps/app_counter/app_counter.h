#pragma once
#include <apps/common/ui/tool_app.h>

namespace toolbox {
class AppCounter final : public ToolApp {
  public:
    AppCounter();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

  private:
    struct State : ToolPage {
        uint64_t count = 0;
    } state_;
    ExtensionViewState view_state() const;
    bool act(const ExtensionEvent& event, int64_t now) override;
    bool key_action(AppKey key, int64_t now) override;
    DeferredSettings settings_;
    void load();
    void save();
};
} // namespace toolbox
