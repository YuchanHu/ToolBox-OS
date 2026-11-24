#include "view.h"
#include <system/ui.h>
#include <system/shell.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <src/core/lv_obj_draw_private.h>
#include <src/misc/lv_area_private.h>
#include <algorithm>
#include <string_view>
extern "C" {
LV_IMAGE_DECLARE(icon_indicator_left);
LV_IMAGE_DECLARE(icon_indicator_right);
}

using namespace toolbox;
namespace {
constexpr const char* drawer_order_key = "drawer_order";
constexpr int page_width = 466;
constexpr int card_size = 170;
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
    saved_order_ = Settings("toolbox").GetString(drawer_order_key);
    if (!saved_order_.empty()) {
        std::vector<std::string_view> order;
        std::string_view remaining(saved_order_);
        while (!remaining.empty()) {
            const auto newline = remaining.find('\n');
            order.push_back(remaining.substr(0, newline));
            if (newline == std::string_view::npos) break;
            remaining.remove_prefix(newline + 1);
        }
        // App IDs are allocated when registering the apps. Restore by name so
        // a change in registration order cannot assign a saved position to another app.
        std::stable_sort(apps_.begin(), apps_.end(), [&order](const auto& a, const auto& b) {
            return std::find(order.begin(), order.end(), a.info.name) <
                   std::find(order.begin(), order.end(), b.info.name);
        });
    }
    keys_ = std::make_unique<input::KeyManager>();
    panel_ = ui::panel(lv_screen_active(), 1, 0, 466, 466);
    ui::heading(panel_, "应用", [](lv_event_t*) { shell_lock(); });
    scroll_ = ui::panel(panel_, 0, 140, 466, 186);
    lv_obj_add_flag(scroll_, static_cast<lv_obj_flag_t>(
        LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(scroll_, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(scroll_, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(scroll_, LV_SCROLLBAR_MODE_OFF);
    const int count = apps_.size();
    if (count) {
        // Maintain scroll bounds independently of the movable snap targets.
        ui::panel(scroll_, last_page * page_width, 0, page_width, 1);
        for (auto& slot : slots_) {
            slot.item = ui::panel(scroll_, 0, 0, page_width, 186);
            lv_obj_set_style_bg_opa(slot.item, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(slot.item, LV_OBJ_FLAG_SNAPPABLE);
            slot.card = ui::panel(slot.item, 0, 0, card_size, card_size);
            lv_obj_set_style_bg_opa(slot.card, LV_OPA_TRANSP, 0);
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
    name_ = ui::text(panel_, count ? apps_[0].info.name.c_str() : "", 0, 326, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(name_, page_width, LV_PART_MAIN);
    lv_obj_set_align(name_, LV_ALIGN_TOP_MID);
    dots_ = ui::panel(panel_, 0, 381, 466, 24);
    for (int index = 0; index < count; ++index) {
        ui::panel(dots_, 0, 0, 6, 6, ui::accent, 6);
        update_dot(index, false);
    }
    if (count) {
        bind_slots(middle_page);
        lv_obj_update_layout(scroll_);
        lv_obj_scroll_to_x(scroll_, middle_page * page_width, LV_ANIM_OFF);
        scroll_x_ = lv_obj_get_scroll_x(scroll_);
        select_page();
        lv_obj_add_event_cb(scroll_, [](lv_event_t* event) {
            auto* view = static_cast<LauncherView*>(lv_event_get_user_data(event));
            const int previous = view->scroll_x_;
            const int current = lv_obj_get_scroll_x(view->scroll_);
            view->scroll_x_ = current;
            view->select_page();
            auto* invalidate = static_cast<bool*>(lv_event_get_param(event));
            if (invalidate && std::abs(current - previous) < page_width / 2) {
                view->invalidate_moving_cards(previous, current);
                *invalidate = false;
            }
        }, LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(scroll_, [](lv_event_t* event) {
            auto* view = static_cast<LauncherView*>(lv_event_get_user_data(event));
            shell_drawer_settled(view->root());
        }, LV_EVENT_SCROLL_END, this);
        lv_obj_add_event_cb(scroll_, draw_scroll_background, LV_EVENT_DRAW_MAIN_BEGIN, this);
        lv_obj_add_event_cb(scroll_, draw_scroll_background, LV_EVENT_DRAW_MAIN_END, this);
    }
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

void view::LauncherView::draw_scroll_background(lv_event_t* event)
{
    auto& view = *static_cast<LauncherView*>(lv_event_get_user_data(event));
    auto* layer = lv_event_get_layer(event);
    if (lv_event_get_code(event) == LV_EVENT_DRAW_MAIN_END) {
        if (view.scroll_clip_) layer->_clip_area = *view.scroll_clip_;
        view.scroll_clip_.reset();
        return;
    }
    if (layer->opa < LV_OPA_MAX) return;
    std::array<lv_area_t, 12> uncovered{layer->_clip_area};
    size_t count = 1;
    for (const auto& slot : view.slots_) {
        const auto* image = static_cast<const lv_image_dsc_t*>(lv_image_get_src(slot.artwork));
        if (!image || lv_color_format_has_alpha(static_cast<lv_color_format_t>(image->header.cf)) ||
            lv_image_get_scale(slot.artwork) != LV_SCALE_NONE) continue;
        lv_area_t art, card;
        lv_obj_get_coords(slot.artwork, &art);
        lv_obj_get_coords(slot.card, &card);
        if (!lv_area_intersect(&art, &art, &card)) continue;
        decltype(uncovered) remaining;
        size_t size = 0;
        for (size_t i = 0; i < count; ++i) {
            lv_area_t pieces[4];
            int n = lv_area_diff(pieces, &uncovered[i], &art);
            if (n < 0) {
                pieces[0] = uncovered[i];
                n = 1;
            }
            if (size + n > remaining.size()) return;
            std::copy_n(pieces, n, remaining.begin() + size);
            size += n;
        }
        std::copy_n(remaining.begin(), size, uncovered.begin());
        count = size;
    }
    lv_draw_rect_dsc_t black;
    lv_draw_rect_dsc_init(&black);
    black.bg_color = lv_color_black();
    for (size_t i = 0; i < count; ++i) lv_draw_rect(layer, &black, &uncovered[i]);
    // The images are opaque. Paint only the gaps and let LVGL draw the cards.
    view.scroll_clip_ = layer->_clip_area;
    layer->_clip_area = {0, 0, -1, -1};
}

void view::LauncherView::invalidate_moving_cards(int previous, int current)
{
    lv_area_t viewport;
    lv_obj_get_coords(scroll_, &viewport);
    const int first = std::min(previous, current);
    const int last = std::max(previous, current);
    const int top = viewport.y1 + (lv_area_get_height(&viewport) - card_size) / 2;
    for (int page = first / page_width; page <= (last + page_width - 1) / page_width; ++page) {
        const int left = viewport.x1 + page * page_width + (page_width - card_size) / 2;
        const lv_area_t swept{left - last, top, left - first + card_size - 1, top + card_size - 1};
        lv_obj_invalidate_area(scroll_, &swept);
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
    const int previous = selected_;
    auto* display = lv_obj_get_display(panel_);
    lv_area_t footer;
    auto add_to_footer = [&](lv_obj_t* object) {
        lv_area_t area;
        lv_obj_get_coords(object, &area);
        const auto margin = lv_obj_get_ext_draw_size(object);
        lv_area_increase(&area, margin, margin);
        lv_area_join(&footer, &footer, &area);
    };
    if (previous >= 0) {
        lv_obj_get_coords(name_, &footer);
        const auto margin = lv_obj_get_ext_draw_size(name_);
        lv_area_increase(&footer, margin, margin);
        add_to_footer(lv_obj_get_child(dots_, previous));
        lv_display_enable_invalidation(display, false);
        update_dot(previous, false);
    }
    selected_ = next;
    lv_label_set_text_static(name_, apps_[selected_].info.name.c_str());
    update_dot(selected_, true);
    if (previous >= 0) {
        // The title and indicators change together. Submit one dirty region
        // for the footer so LVGL traverses it once at the page crossing.
        lv_obj_update_layout(panel_);
        lv_display_enable_invalidation(display, true);
        add_to_footer(name_);
        add_to_footer(lv_obj_get_child(dots_, selected_));
        lv_obj_invalidate_area(panel_, &footer);
    }
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
        onAppClicked(id);
    }
}

bool view::LauncherView::remember_app(int app_id)
{
    const auto selected = std::find_if(apps_.begin(), apps_.end(), [app_id](const auto& app) {
        return app.appID == app_id;
    });
    if (selected == apps_.end()) return false;

    std::string next_order = selected->info.name + '\n';
    for (const auto& app : apps_) {
        if (app.appID != app_id) next_order += app.info.name + '\n';
    }
    if (next_order == saved_order_) return true;

    Settings settings("toolbox", true);
    if (settings.TrySetString(drawer_order_key, next_order) != ESP_OK) return false;
    saved_order_ = std::move(next_order);
    return true;
}


