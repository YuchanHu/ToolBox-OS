#pragma once

#include <cstdint>

#if defined(ESP_PLATFORM)
#include <sdkconfig.h>
#endif
#ifdef CONFIG_TOOLBOX_PERF
#include <esp_timer.h>
#endif

namespace toolbox::perf {

enum Metric {
    touch_sample_interval, touch_read_time, touch_read_error,
    touch_queue_depth, touch_queue_delay, touch_overflow,
    gui_lock_wait, gui_lock_hold, event_process_time, snapshot_time,
    render_time, render_interval, buffer_sync_time, te_wait_time, te_period,
    te_phase, te_missed, te_timeout, dma_pack_time, dma_transfer_time,
    dma_bursts, dma_over_budget, dirty_pixels, transfer_pixels, frame_drop,
    input_to_present, present_interval, active_present_interval, count
};

#ifdef CONFIG_TOOLBOX_PERF
inline uint32_t now() { return static_cast<uint32_t>(esp_timer_get_time()); }
void sample(Metric metric, uint32_t value);
void report(uint32_t screen_pixels);
#else
inline uint32_t now() { return 0; }
inline void sample(Metric, uint32_t) {}
inline void report(uint32_t) {}
#endif

} // namespace toolbox::perf
