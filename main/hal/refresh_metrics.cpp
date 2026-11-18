#include "refresh_metrics.h"

#ifdef CONFIG_TOOLBOX_PERF
#include <algorithm>
#include <array>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace toolbox::perf {
namespace {
constexpr const char* names[] = {
    "touch_sample_interval", "touch_read_time", "touch_read_error",
    "touch_queue_depth", "touch_queue_delay", "touch_overflow",
    "gui_lock_wait", "gui_lock_hold", "event_process_time", "snapshot_time",
    "render_time", "render_interval", "buffer_sync_time", "te_wait_time", "te_period",
    "te_phase", "te_missed", "te_timeout", "dma_pack_time", "dma_transfer_time",
    "dma_bursts", "dma_over_budget", "dirty_pixels", "transfer_pixels", "frame_drop",
    "input_to_present", "present_interval", "active_present_interval"
};
static_assert(std::size(names) == count);
constexpr unsigned subdivisions = 4;
constexpr unsigned buckets = 32 * subdivisions;

struct Histogram {
    uint64_t sum = 0;
    uint32_t total = 0, max = 0;
    std::array<uint16_t, buckets> bins{};

    uint32_t percentile(uint32_t percent) const
    {
        const uint32_t target = (total * percent + 99) / 100;
        uint32_t accumulated = 0;
        for (unsigned i = 0; i < bins.size(); ++i) {
            accumulated += bins[i];
            if (accumulated >= target) {
                const uint64_t scale = uint64_t{1} << (i / subdivisions);
                const uint64_t upper = scale + (scale * (i % subdivisions + 1) - 1) / subdivisions;
                return static_cast<uint32_t>(std::min<uint64_t>(max, upper));
            }
        }
        return max;
    }
};
std::array<Histogram, count> stats;
struct Summary {
    uint64_t sum;
    uint32_t total, max, p95, p99;
};
std::array<Summary, count> report_stats;
portMUX_TYPE stats_mux = portMUX_INITIALIZER_UNLOCKED;

#if defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) && defined(CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER)
void report_cpu(uint32_t interval)
{
    static std::array<uint32_t, 6> previous{};
    static bool ready = false;
    uint32_t current[6] = {
        ulTaskGetIdleRunTimeCounterForCore(0), ulTaskGetIdleRunTimeCounterForCore(1)
    };
    const char* task_names[] = {"main", "lvgl", "touch_sample", "display_dma"};
    for (unsigned i = 0; i < std::size(task_names); ++i) {
        if (auto handle = xTaskGetHandle(task_names[i])) {
            TaskStatus_t task{};
            vTaskGetInfo(handle, &task, pdFALSE, eInvalid);
            current[i + 2] = task.ulRunTimeCounter;
        }
    }
    double utilization[6];
    for (unsigned i = 0; i < previous.size(); ++i) {
        utilization[i] = std::clamp(100.0 * (current[i] - previous[i]) / interval, 0.0, 100.0);
        previous[i] = current[i];
    }
    if (ready) {
        ESP_LOGI("Refresh", "cpu0=%.1f%% cpu1=%.1f%% runtime=%.1f%% lvgl=%.1f%% touch=%.1f%% dma=%.1f%%",
            100 - utilization[0], 100 - utilization[1], utilization[2], utilization[3], utilization[4], utilization[5]);
    }
    ready = true;
}
#endif
}

void sample(Metric metric, uint32_t value)
{
    const uint32_t nonzero = std::max<uint32_t>(value, 1);
    const unsigned exponent = 31 - __builtin_clz(nonzero);
    const unsigned subdivision = exponent >= 2
        ? (nonzero >> (exponent - 2)) - subdivisions
        : ((nonzero - (1u << exponent)) * subdivisions) >> exponent;
    portENTER_CRITICAL(&stats_mux);
    auto& stat = stats[metric];
    ++stat.bins[exponent * subdivisions + subdivision];
    ++stat.total;
    stat.max = std::max(stat.max, value);
    stat.sum += value;
    portEXIT_CRITICAL(&stats_mux);
}

void report(uint32_t screen_pixels)
{
    static uint32_t previous = now();
    static uint32_t interval, last_log;
    static unsigned next = count;
    const uint32_t current = now();
    if (current - previous >= 5000000 && next == count) {
        interval = current - previous;
        previous = current;
        for (unsigned i = 0; i < stats.size(); ++i) {
            portENTER_CRITICAL(&stats_mux);
            const auto stat = stats[i];
            stats[i] = {};
            portEXIT_CRITICAL(&stats_mux);
            report_stats[i] = {stat.sum, stat.total, stat.max, stat.percentile(95), stat.percentile(99)};
        }
        next = 0;
        last_log = current;
        ESP_LOGI("Refresh", "window_us=%lu internal_heap=%u largest_internal_block=%u psram_free=%u",
            static_cast<unsigned long>(interval), heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#if defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) && defined(CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER)
        report_cpu(interval);
#endif
    }
    if (current - last_log < 100000) return;
    while (next < count) {
        const unsigned i = next++;
        const auto& stat = report_stats[i];
        if (!stat.total) continue;
        last_log = current;
        ESP_LOGI("Refresh", "%s n=%lu avg=%llu max=%lu p95<=%lu p99<=%lu",
            names[i], static_cast<unsigned long>(stat.total), stat.sum / stat.total,
            static_cast<unsigned long>(stat.max), static_cast<unsigned long>(stat.p95),
            static_cast<unsigned long>(stat.p99));
        if (i == dirty_pixels)
            ESP_LOGI("Refresh", "dirty_ratio=%.1f%% render_fps=%.1f",
                100.0 * stat.sum / stat.total / screen_pixels, 1000000.0 * stat.total / interval);
        if (i == dma_transfer_time)
            ESP_LOGI("Refresh", "present_fps=%.1f", 1000000.0 * stat.total / interval);
        if (i == active_present_interval && stat.sum)
            ESP_LOGI("Refresh", "active_present_fps=%.1f", 1000000.0 * stat.total / stat.sum);
        return;
    }
}

} // namespace toolbox::perf
#endif
