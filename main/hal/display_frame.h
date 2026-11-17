#pragma once
#include <lvgl.h>
#include <algorithm>
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
    const int pairs_per_row = (area.x2 - area.x1 + 1) / 2;
    auto* output = reinterpret_cast<PixelPair*>(destination);
    for (int row = 0; row < rows; ++row) {
        const auto* source = reinterpret_cast<const PixelPair*>(
            frame + (first_y + row) * stride + area.x1 * 2);
        for (int pair = 0; pair < pairs_per_row; ++pair) {
            const uint32_t pixels = source[pair];
            output[pair] = swap_bytes
                ? ((pixels & 0x00ff00ffU) << 8) | ((pixels & 0xff00ff00U) >> 8)
                : pixels;
        }
        output += pairs_per_row;
    }
}

} // namespace toolbox::display
