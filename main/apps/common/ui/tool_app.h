#pragma once
#include <mooncake.h>
#include <system/extension_ui.h>
#include <memory>

namespace toolbox {
struct ToolPage {
    ExtensionPage page = ExtensionPage::Main;
    bool confirm = false;
};

class ToolApp : public mooncake::AppAbility {
  public:
    void onCreate() override {}

  protected:
    ToolApp(const char* name, const lv_image_dsc_t& icon);
    void open_view(Extension kind, const ExtensionViewState& state);
    bool handle_input(ToolPage& page, int64_t now);
    bool view_due(int64_t now);
    void update_view(const ExtensionViewState& state);
    void close_view();
    virtual bool act(const ExtensionEvent& event, int64_t now) = 0;
    virtual bool key_action(AppKey key, int64_t now) = 0;
    virtual void on_back() {}

  private:
    AppKey key();
    std::unique_ptr<ExtensionView> view_;
    bool combo_ = false;
    int64_t updated_ = 0;
};

class DeferredSettings {
  public:
    void changed() { dirty_ = true; }
    template<class Save> void update(int64_t now, Save save)
    {
        if (!dirty_) return;
        if (!due_) due_ = now + 1000000;
        if (now >= due_) flush(save);
    }
    template<class Save> void flush(Save save)
    {
        if (dirty_) save();
        dirty_ = false;
        due_ = 0;
    }

  private:
    bool dirty_ = false;
    int64_t due_ = 0;
};
} // namespace toolbox
