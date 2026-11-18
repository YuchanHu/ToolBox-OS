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
constexpr int page_width = 466;
constexpr int middle_page = 40;
constexpr int last_page = middle_page * 2;

int wrap(int index, int count)
{
    return (index % count + count) % count;
}
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
    if (count) {
        // Maintain scroll bounds independently of the movable snap targets.
        ui::panel(scroll_, last_page * page_width, 0, page_width, 1);
        for (auto& slot : slots_) {
            slot.item = ui::panel(scroll_, 0, 0, page_width, 186);
            lv_obj_add_flag(slot.item, LV_OBJ_FLAG_SNAPPABLE);
            slot.card = ui::panel(slot.item, 0, 0, 170, 170);
            lv_obj_center(slot.card);
            lv_obj_add_flag(slot.card, LV_OBJ_FLAG_CLICKABLE);
            slot.artwork = lv_image_create(slot.card);
            lv_obj_align(slot.artwork, LV_ALIGN_CENTER, 0, 4);
            lv_obj_remove_flag(slot.artwork, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(slot.card, [](lv_event_t* event) {
                auto* self = static_cast<LauncherView*>(lv_event_get_user_data(event));
                auto* card = static_cast<lv_obj_t*>(lv_event_get_target(event));
                auto* slot = static_cast<Slot*>(lv_obj_get_user_data(card));
                self->pending_app_ = self->apps_[slot->app].appID;
            }, LV_EVENT_CLICKED, this);
            lv_obj_set_user_data(slot.card, &slot);
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
    for (int index = 0; index < count; ++index) {
        ui::panel(dots_, 0, 0, 6, 6, ui::accent, 6);
        update_dot(index, false);
    }
    if (count) {
        bind_slots(middle_page);
        lv_obj_update_layout(scroll_);
        lv_obj_scroll_to_x(scroll_, middle_page * page_width, LV_ANIM_OFF);
        select_page();
        lv_obj_add_event_cb(scroll_, [](lv_event_t* event) {
            static_cast<LauncherView*>(lv_event_get_user_data(event))->select_page();
        }, LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(scroll_, [](lv_event_t* event) {
            auto* view = static_cast<LauncherView*>(lv_event_get_user_data(event));
            shell_drawer_settled(view->root());
        }, LV_EVENT_SCROLL_END, this);
    }
    GetHAL().bootLogo.reset();
}

void view::LauncherView::bind_slots(int page)
{
    const int count = apps_.size();
    for (int index = page - 2; index <= page + 2; ++index) {
        auto& slot = slots_[wrap(index, slots_.size())];
        if (slot.page == index) continue;
        slot.page = index;
        lv_obj_set_x(slot.item, index * page_width);
        const int app = wrap(index - middle_page, count);
        if (slot.app == app) continue;
        slot.app = app;
        const auto* image = apps_[app].info.icon ? static_cast<const lv_image_dsc_t*>(apps_[app].info.icon)
                                               : &toolbox_icon_box_78;
        lv_image_set_src(slot.artwork, image);
        lv_image_set_pivot(slot.artwork, image->header.w / 2, image->header.h / 2);
        lv_image_set_scale(slot.artwork, image == &toolbox_icon_box_78 ? 282 : LV_SCALE_NONE);
    }
}

void view::LauncherView::update_dot(int index, bool active)
{
    auto* dot = lv_obj_get_child(dots_, index);
    const int size = active ? 12 : 6;
    lv_obj_set_size(dot, size, size);
    lv_obj_set_pos(dot, 233 - (static_cast<int>(apps_.size()) - 1) * 8 + index * 16 - size / 2, 12 - size / 2);
    lv_obj_set_style_opa(dot, active ? LV_OPA_COVER : LV_OPA_50, 0);
}

void view::LauncherView::select_page()
{
    const int live_page = (lv_obj_get_scroll_x(scroll_) + page_width / 2) / page_width;
    bind_slots(live_page);
    const int next = wrap(live_page - middle_page, apps_.size());
    if (next == selected_) return;
    if (selected_ >= 0) update_dot(selected_, false);
    selected_ = next;
    lv_label_set_text(name_, apps_[selected_].info.name.c_str());
    update_dot(selected_, true);
}

void view::LauncherView::step(int direction)
{
    if (apps_.empty()) return;
    const int page = (lv_obj_get_scroll_x(scroll_) + page_width / 2) / page_width;
    lv_obj_scroll_to_x(scroll_, (page + direction) * page_width, LV_ANIM_ON);
}

void view::LauncherView::update()
{
    if (apps_.empty()) return;
    const auto key = keys_->update();
    if (key == input::KeyEvent::GoPrevious) step(-1);
    if (key == input::KeyEvent::GoNext) step(1);
    if (key == input::KeyEvent::GoHome) shell_lock();
    const int x = lv_obj_get_scroll_x(scroll_);
    const int page = (x + page_width / 2) / page_width;
    if ((page < middle_page / 2 || page > middle_page * 3 / 2) &&
        !lv_obj_is_scrolling(scroll_) &&
        (!GetHAL().lvTouchpad || !lv_indev_get_scroll_obj(GetHAL().lvTouchpad))) {
        const int rebased = middle_page + selected_;
        bind_slots(rebased);
        lv_obj_update_layout(scroll_);
        lv_obj_scroll_to_x(scroll_, rebased * page_width + x - page * page_width, LV_ANIM_OFF);
    }
    if (pending_app_ >= 0 && onAppClicked) {
        const int id = pending_app_;
        pending_app_ = -1;
        recent_apps.erase(std::remove(recent_apps.begin(), recent_apps.end(), id), recent_apps.end());
        recent_apps.insert(recent_apps.begin(), id);
        onAppClicked(id);
    }
}



