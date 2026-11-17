#pragma once
#include <hal/touch_history.h>
#include <lvgl.h>

namespace toolbox {
void route_touch_sample(lv_indev_t* input, lv_indev_data_t* data,
                        const TouchSample& sample, bool more);
}
