#pragma once
#include <lvgl.h>
#include <mooncake.h>
#include <apps/common/key_manager/key_manager.h>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace view {
class LauncherView {
public:
    ~LauncherView();
    std::function<void(int)> onAppClicked;
    void init(std::vector<mooncake::AppProps_t> apps);
    void update();
    bool remember_app(int app_id);
    lv_obj_t* root() const { return panel_; }
private:
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* scroll_ = nullptr;
    lv_obj_t* name_ = nullptr;
    lv_obj_t* dots_ = nullptr;
    std::vector<mooncake::AppProps_t> apps_;
    std::string saved_order_;
    std::unique_ptr<input::KeyManager> keys_;
    struct Slot {
        lv_obj_t* item = nullptr;
        lv_obj_t* card = nullptr;
        lv_obj_t* artwork = nullptr;
        int page = -100, app = -1;
    };
    std::array<Slot, 5> slots_;
    void bind_slots(int page);
    void select_page();
    static void draw_scroll_background(lv_event_t* event);
    void invalidate_moving_cards(int previous, int current);
    void update_dot(int index, bool active);
    int selected_ = -1;
    int scroll_x_ = 0;
    std::optional<lv_area_t> scroll_clip_;
    int pending_app_ = -1;
    void step(int direction);
};
}
