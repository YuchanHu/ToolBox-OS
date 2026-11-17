#pragma once
#include <lvgl.h>
#include <mooncake.h>
#include <apps/common/key_manager/key_manager.h>
#include <functional>
#include <memory>
#include <vector>

namespace view {
class LauncherView {
public:
    ~LauncherView();
    std::function<void(int)> onAppClicked;
    void init(std::vector<mooncake::AppProps_t> apps);
    void update();
    lv_obj_t* root() const { return panel_; }
private:
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* scroll_ = nullptr;
    lv_obj_t* name_ = nullptr;
    lv_obj_t* dots_ = nullptr;
    std::vector<mooncake::AppProps_t> apps_;
    std::unique_ptr<input::KeyManager> keys_;
    int selected_ = -1;
    int pending_app_ = -1;
    void step(int direction);
};
}
