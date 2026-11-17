#pragma once
#include "camera_remote.h"
#include "ui.h"

namespace toolbox {
class CameraPage {
public:
    enum class Screen { Modes, Photo, Movie };
    enum class ActionType { None, Back, Rescan, Select, Photo, Movie, Trigger };
    struct Action { ActionType type = ActionType::None; camera::Device device; };
    CameraPage();
    ~CameraPage();
    Action take_action();
    void update(const camera::Status& status, Screen screen, uint32_t now);
private:
    lv_obj_t *root_, *title_, *message_, *detail_, *devices_, *modes_, *photo_page_, *movie_page_;
    lv_obj_t *photo_action_, *time_, *rescan_;
    Action pending_;
    camera::Status previous_;
    uint32_t previous_second_ = UINT32_MAX;
    bool initialized_ = false;
    bool touch_down_ = false, controls_visible_ = false;
    bool reveal_requested_ = false;
    uint32_t controls_at_ = 0;
    Screen previous_screen_ = Screen::Modes;
    static void event(lv_event_t* event);
    void rebuild_devices(const camera::Status& status);
};
}
