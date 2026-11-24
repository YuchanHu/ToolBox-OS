#pragma once
#include <hal/touch_history.h>
#include <lvgl.h>

namespace toolbox {
bool touch_input_blocked();
void set_touch_input_blocked(lv_indev_t* input, bool blocked);
void route_touch_sample(lv_indev_t* input, lv_indev_data_t* data,
                        const TouchSample& sample, bool more);
}
