#include "view.h"
#include <system/ui.h>
#include <system/shell.h>
#include <hal/hal.h>
#include <algorithm>
extern "C" {
LV_IMAGE_DECLARE(icon_indicator_left);
LV_IMAGE_DECLARE(icon_indicator_right);
}

using namespace toolbox;
namespace {
std::vector<int> recent_apps;
}

view::LauncherView::~LauncherView()
{
    if (panel_) lv_obj_delete(panel_);
}

void view::LauncherView::init(std::vector<mooncake::AppProps_t> apps)
{
    apps_ = std::move(apps);
    std::stable_sort(apps_.begin(), apps_.end(), [](const auto& a, const auto& b) {
        return std::find(recent_apps.begin(), recent_apps.end(), a.appID) <
               std::find(recent_apps.begin(), recent_apps.end(), b.appID);
    });
    keys_ = std::make_unique<input::KeyManager>();
    panel_ = ui::panel(lv_screen_active(), 1, 0, 466, 466);
    ui::heading(panel_, "应用", [](lv_event_t*) { shell_lock(); });
    scroll_ = ui::panel(panel_, 0, 140, 466, 186);
    lv_obj_add_flag(scroll_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE));
    lv_obj_set_scroll_dir(scroll_, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(scroll_, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(scroll_, LV_SCROLLBAR_MODE_OFF);
    const int count = apps_.size();
    for (int copy = 0; copy < 5; ++copy) {
        for (int index = 0; index < count; ++index) {
            auto* item = ui::panel(scroll_, (copy * count + index) * 466, 0, 466, 186);
            lv_obj_add_flag(item, LV_OBJ_FLAG_SNAPPABLE);
            const lv_image_dsc_t* image = apps_[index].info.icon ? static_cast<const lv_image_dsc_t*>(apps_[index].info.icon)
                : &toolbox_icon_box_78;
            auto* card = ui::panel(item, 0, 0, 170, 170);
            lv_obj_center(card);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            auto* artwork = lv_image_create(card);
            lv_image_set_src(artwork, image);
            lv_image_set_pivot(artwork, image->header.w / 2, image->header.h / 2);
            lv_image_set_scale(artwork, 282);
            // The generated silhouettes sit slightly above their canvas center.
            lv_obj_align(artwork, LV_ALIGN_CENTER, 0, 4);
            lv_obj_remove_flag(artwork, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(card, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
            lv_obj_add_event_cb(card, [](lv_event_t* event) {
                auto* self = static_cast<LauncherView*>(lv_event_get_user_data(event));
                auto* card = static_cast<lv_obj_t*>(lv_event_get_target(event));
                self->pending_app_ = self->apps_[reinterpret_cast<uintptr_t>(lv_obj_get_user_data(card))].appID;
            }, LV_EVENT_CLICKED, this);
        }
    }
    auto* previous = ui::icon_button(panel_, &icon_indicator_left, 7, 153, 52, 160, [](lv_event_t*) {});
    auto* next = ui::icon_button(panel_, &icon_indicator_right, 407, 153, 52, 160, [](lv_event_t*) {});
    for (auto* arrow : {previous, next}) {
        lv_obj_set_style_bg_opa(arrow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(arrow, 0, 0);
        auto* image = lv_obj_get_child(arrow, 1);
        lv_obj_set_style_image_recolor(image, lv_color_white(), 0);
        lv_obj_center(image);
    }
    lv_obj_add_event_cb(previous, [](lv_event_t* e) { static_cast<LauncherView*>(lv_event_get_user_data(e))->step(-1); }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(next, [](lv_event_t* e) { static_cast<LauncherView*>(lv_event_get_user_data(e))->step(1); }, LV_EVENT_CLICKED, this);
    name_ = ui::text(panel_, count ? apps_[0].info.name.c_str() : "", 0, 326, 466);
    dots_ = ui::panel(panel_, 0, 381, 466, 24);
    for (int index = 0; index < count; ++index) ui::panel(dots_, 0, 0, 8, 8, ui::accent, 6);
    lv_obj_update_layout(scroll_);
    lv_obj_scroll_to_x(scroll_, 2 * count * 466, LV_ANIM_OFF);
    GetHAL().bootLogo.reset();
    update();
}

void view::LauncherView::step(int direction)
{
    const int index = (lv_obj_get_scroll_x(scroll_) + 233) / 466;
    lv_obj_scroll_to_x(scroll_, (index + direction) * 466, LV_ANIM_ON);
}

void view::LauncherView::update()
{
    const int count = apps_.size();
    if (count == 0) return;
    const auto key = keys_->update();
    if (key == input::KeyEvent::GoPrevious) step(-1);
    if (key == input::KeyEvent::GoNext) step(1);
    if (key == input::KeyEvent::GoHome) shell_lock();
    int x = lv_obj_get_scroll_x(scroll_);
    if (!lv_obj_is_scrolling(scroll_) && !lv_indev_get_scroll_obj(GetHAL().lvTouchpad)) {
        const int span = count * 466;
        if (x < span || x >= 4 * span) {
            x = 2 * span + ((x % span) + span) % span;
            lv_obj_scroll_to_x(scroll_, x, LV_ANIM_OFF);
        }
    }
    const int selected = ((x + 233) / 466) % count;
    if (selected != selected_) {
        selected_ = selected;
        lv_label_set_text(name_, apps_[selected_].info.name.c_str());
        for (int index = 0; index < count; ++index) {
            auto* dot = lv_obj_get_child(dots_, index);
            const int size = index == selected_ ? 12 : 6;
            lv_obj_set_size(dot, size, size);
            lv_obj_set_pos(dot, 233 - (count - 1) * 8 + index * 16 - size / 2, 12 - size / 2);
            lv_obj_set_style_opa(dot, index == selected_ ? LV_OPA_COVER : LV_OPA_50, 0);
        }
    }
    if (pending_app_ >= 0 && onAppClicked) {
        const int id = pending_app_;
        pending_app_ = -1;
        recent_apps.erase(std::remove(recent_apps.begin(), recent_apps.end(), id), recent_apps.end());
        recent_apps.insert(recent_apps.begin(), id);
        onAppClicked(id);
    }
}



