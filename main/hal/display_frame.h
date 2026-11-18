#pragma once
#include <lvgl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace toolbox::display {

// CO5300 windows require even origins and dimensions. Full-screen buffers also
// contain any neighboring pixels included by this alignment.
inline lv_area_t aligned_area(lv_area_t area, int width, int height)
{
    area.x1 = std::max<int32_t>(0, area.x1) & ~1;
    area.y1 = std::max<int32_t>(0, area.y1) & ~1;
    area.x2 = std::min<int32_t>(width - 1, area.x2 | 1);
    area.y2 = std::min<int32_t>(height - 1, area.y2 | 1);
    return area;
}

inline lv_area_t bounds(const lv_area_t& a, const lv_area_t& b)
{
    return {std::min(a.x1, b.x1), std::min(a.y1, b.y1),
            std::max(a.x2, b.x2), std::max(a.y2, b.y2)};
}

struct FrameRegions {
    std::array<lv_area_t, 8> areas{};
    unsigned count = 0;

    void add(lv_area_t area)
    {
        int best_cost = INT32_MAX;
        unsigned best = 0;
        for (unsigned i = 0; i < count;) {
            const auto combined = bounds(areas[i], area);
            const int cost = static_cast<int>(lv_area_get_size(&combined)) -
                static_cast<int>(lv_area_get_size(&areas[i])) - static_cast<int>(lv_area_get_size(&area));
            if (cost <= 0) {
                area = combined;
                areas[i] = areas[--count];
                i = 0;
                best_cost = INT32_MAX;
                continue;
            }
            if (cost < best_cost) {
                best_cost = cost;
                best = i;
            }
            ++i;
        }
        if (count == areas.size()) {
            areas[best] = bounds(areas[best], area);
        } else {
            areas[count++] = area;
        }
    }
};

// Frame, stride, even-x windows and DMA buffers are all 4-byte aligned.
// may_alias permits reading LVGL's byte storage as pairs of RGB565 pixels.
#if defined(__GNUC__)
using PixelPair = uint32_t __attribute__((__may_alias__));
#else
using PixelPair = uint32_t;
#endif

inline void pack_rows(uint8_t* destination, const uint8_t* frame, uint32_t stride,
                      const lv_area_t& area, int first_y, int rows, bool swap_bytes = true)
{
    const size_t row_bytes = (area.x2 - area.x1 + 1) * 2;
    if (!swap_bytes) {
        const auto* source = frame + first_y * stride + area.x1 * 2;
        if (row_bytes == stride) {
            std::memcpy(destination, source, row_bytes * rows);
        } else {
            for (int row = 0; row < rows; ++row)
                std::memcpy(destination + row * row_bytes, source + row * stride, row_bytes);
        }
        return;
    }
    const int pairs_per_row = (area.x2 - area.x1 + 1) / 2;
    auto* output = reinterpret_cast<PixelPair*>(destination);
    for (int row = 0; row < rows; ++row) {
        const auto* source = reinterpret_cast<const PixelPair*>(
            frame + (first_y + row) * stride + area.x1 * 2);
        for (int pair = 0; pair < pairs_per_row; ++pair) {
            const uint32_t pixels = source[pair];
            output[pair] = ((pixels & 0x00ff00ffU) << 8) | ((pixels & 0xff00ff00U) >> 8);
        }
        output += pairs_per_row;
    }
}

} // namespace toolbox::display
