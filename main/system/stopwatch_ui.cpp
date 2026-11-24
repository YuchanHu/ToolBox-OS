#include "stopwatch_ui.h"
namespace toolbox {
StopwatchPage::StopwatchPage(std::function<void()> back, std::function<void()> left, std::function<void()> toggle)
    : back_(std::move(back)), left_(std::move(left)), toggle_(std::move(toggle))
{
    root_ = ui::panel(lv_screen_active(), 1, 0, 466, 466);
    ui::heading(root_, "秒表", [](lv_event_t*) {});
    auto* back_button = lv_obj_get_child(root_, 1);
    lv_obj_add_event_cb(back_button, [](lv_event_t* e) { static_cast<StopwatchPage*>(lv_event_get_user_data(e))->back_(); }, LV_EVENT_CLICKED, this);
    time_ = ui::text(root_, "00:00", 0, 164, 466, &toolbox_misans_65);
    lv_obj_set_style_text_letter_space(time_, -2, 0);
    fraction_ = ui::text(root_, ".00", 0, 249, 466, &toolbox_misans_28, 0x9cbde9);
    left_button_ = ui::button(root_, "计圈", 94, 319, 130, 61, [](lv_event_t*) {});
    lv_obj_set_style_radius(left_button_, 30, 0);
    lv_obj_add_event_cb(left_button_, [](lv_event_t* e) { static_cast<StopwatchPage*>(lv_event_get_user_data(e))->left_(); }, LV_EVENT_CLICKED, this);
    action_ = ui::button(root_, "开始", 240, 319, 130, 61, [](lv_event_t*) {}, ui::accent);
    lv_obj_set_style_radius(action_, 30, 0);
    lv_obj_add_event_cb(action_, [](lv_event_t* e) { static_cast<StopwatchPage*>(lv_event_get_user_data(e))->toggle_(); }, LV_EVENT_CLICKED, this);
    records_button_ = ui::button(root_, "记录 0", 163, 390, 140, 42, [](lv_event_t*) {}, 0x202631, &toolbox_misans_20);
    lv_obj_add_event_cb(records_button_, [](lv_event_t* e) {
        auto* self = static_cast<StopwatchPage*>(lv_event_get_user_data(e));
        lv_obj_remove_flag(self->records_page_, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, this);
    records_page_ = ui::panel(root_, 0, 0, 466, 466);
    ui::heading(records_page_, "计圈记录", [](lv_event_t*) {});
    lv_obj_add_event_cb(lv_obj_get_child(records_page_, 1), [](lv_event_t* e) {
        lv_obj_add_flag(static_cast<StopwatchPage*>(lv_event_get_user_data(e))->records_page_, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, this);
    records_list_ = ui::scroll_panel(records_page_, 69, ui::content_top, 328, 232);
    lv_obj_set_scrollbar_mode(records_list_, LV_SCROLLBAR_MODE_OFF);
    ui::cache_scroll_rows(records_list_);
    lv_obj_add_flag(records_page_, LV_OBJ_FLAG_HIDDEN);
}
StopwatchPage::~StopwatchPage() { lv_obj_delete(root_); }
void StopwatchPage::update(uint32_t elapsed, model::Stopwatch::State_t state, const std::vector<uint32_t>& laps)
{
    if (lv_obj_has_flag(records_page_, LV_OBJ_FLAG_HIDDEN)) {
        if (displayed_second_ != elapsed / 1000) {
            displayed_second_ = elapsed / 1000;
            lv_label_set_text_fmt(time_, "%02u:%02u", static_cast<unsigned>(elapsed / 60000), static_cast<unsigned>((elapsed / 1000) % 60));
        }
        if (displayed_fraction_ != (elapsed / 10) % 100) {
            displayed_fraction_ = (elapsed / 10) % 100;
            lv_label_set_text_fmt(fraction_, ".%02u", static_cast<unsigned>(displayed_fraction_));
        }
    }
    if (!state_initialized_ || displayed_state_ != state) {
        state_initialized_ = true;
        displayed_state_ = state;
        lv_label_set_text(lv_obj_get_child(action_, 0), state == model::Stopwatch::State_t::Running ? "暂停" : "开始");
        lv_label_set_text(lv_obj_get_child(left_button_, 0), state == model::Stopwatch::State_t::Paused ? "复位" : "计圈");
        lv_obj_set_state(left_button_, LV_STATE_DISABLED, state == model::Stopwatch::State_t::Stopped);
        lv_obj_set_style_opa(left_button_, state == model::Stopwatch::State_t::Stopped ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
    if (lap_count_ == laps.size()) return;
    lap_count_ = laps.size();
    lv_label_set_text_fmt(lv_obj_get_child(records_button_, 0), "记录 %u", static_cast<unsigned>(lap_count_));
    lv_obj_set_state(records_button_, LV_STATE_DISABLED, laps.empty());
    lv_obj_clean(records_list_);
    for (size_t index = 0; index < laps.size(); ++index) {
        auto* row = ui::panel(records_list_, 0, static_cast<int>(laps.size() - 1 - index) * 58, 328, 50, 0x181d25, 18);
        auto* content = ui::row(row, 16, 0, 296, 50, 10);
        auto* title = ui::row_text(content, "", &toolbox_misans_20, 0x98a4b7);
        lv_label_set_text_fmt(title, "第%u圈", static_cast<unsigned>(index + 1));
        lv_obj_set_flex_grow(title, 1);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
        auto* value = ui::row_text(content, "", &toolbox_misans_20);
        const auto time = laps[index];
        lv_label_set_text_fmt(value, "%02u:%02u:%02u.%02u", static_cast<unsigned>(time / 3600000),
            static_cast<unsigned>(time / 60000 % 60), static_cast<unsigned>(time / 1000 % 60), static_cast<unsigned>(time / 10 % 100));
    }
}
}
