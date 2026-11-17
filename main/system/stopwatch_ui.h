#pragma once
#include "ui.h"
#include <functional>
#include <apps/app_stopwatch/model/stopwatch.h>
namespace toolbox {
class StopwatchPage {
public:
    StopwatchPage(std::function<void()> back, std::function<void()> left, std::function<void()> toggle);
    ~StopwatchPage();
    void update(uint32_t elapsed, model::Stopwatch::State_t state, const std::vector<uint32_t>& laps);
private:
    lv_obj_t* root_;
    lv_obj_t* time_;
    lv_obj_t* fraction_;
    lv_obj_t* action_;
    lv_obj_t* left_button_;
    lv_obj_t* records_button_;
    lv_obj_t* records_page_;
    lv_obj_t* records_list_;
    size_t lap_count_ = static_cast<size_t>(-1);
    uint32_t displayed_second_ = UINT32_MAX;
    uint32_t displayed_fraction_ = UINT32_MAX;
    model::Stopwatch::State_t displayed_state_ = model::Stopwatch::State_t::Stopped;
    bool state_initialized_ = false;
    std::function<void()> back_, left_, toggle_;
};
}
