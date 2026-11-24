#include "touch_input.h"
#include "shell.h"
#include <atomic>

namespace toolbox {
namespace {
std::atomic_bool input_blocked{false};
bool wait_for_release = false;
}

bool touch_input_blocked() { return input_blocked.load(); }

void set_touch_input_blocked(lv_indev_t* input, bool blocked)
{
    if (input_blocked.exchange(blocked) == blocked) return;
    if (input) lv_indev_reset(input, nullptr);
    if (blocked) shell_dismiss_touch_overlays();
}

void route_touch_sample(lv_indev_t* input, lv_indev_data_t* data,
                        const TouchSample& sample, bool more)
{
    data->continue_reading = more;
    data->point = {sample.x, sample.y};
    if (touch_input_blocked() || wait_for_release) {
        wait_for_release = sample.pressed;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
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
