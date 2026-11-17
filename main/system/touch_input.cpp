#include "touch_input.h"
#include "shell.h"

namespace toolbox {
void route_touch_sample(lv_indev_t* input, lv_indev_data_t* data,
                        const TouchSample& sample, bool more)
{
    data->continue_reading = more;
    data->point = {sample.x, sample.y};
    if (sample.cancel) {
        lv_indev_wait_release(input);
        shell_cancel_touch();
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    const bool consumed = shell_touch(sample.pressed, sample.x, sample.y, sample.tick, !more);
    data->state = sample.pressed && !consumed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
}
