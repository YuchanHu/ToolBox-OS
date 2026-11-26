#include "media.h"
#include "lgfx/utility/lgfx_tjpgd.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace sony {
namespace {
void* allocate(size_t size) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::malloc(size);
#endif
}
struct Decode {
    Bytes source;
    MediaImage image;
};
uint32_t input(void* context, uint8_t* destination, uint32_t requested) {
    auto& source = static_cast<Decode*>(context)->source;
    const auto size = std::min<size_t>(requested, source.size);
    if (destination) std::memcpy(destination, source.data, size);
    source = {source.data + size, source.size - size};
    return size;
}
uint32_t output(void* context, void* data, JRECT* rect) {
    const auto& image = static_cast<Decode*>(context)->image;
    if (rect->right >= image.width || rect->bottom >= image.height) return 0;
    auto* rgb = static_cast<const uint8_t*>(data);
    for (auto y = rect->top; y <= rect->bottom; ++y) {
        auto* pixels = image.pixels.get() + y * image.width;
        for (auto x = rect->left; x <= rect->right; ++x, rgb += 3) {
            pixels[x] = ((rgb[0] & 0xf8) << 8) | ((rgb[1] & 0xfc) << 3) | (rgb[2] >> 3);
        }
    }
    return 1;
}
}
MediaImage decodeThumbnail(Bytes bytes) {
    if (bytes.size < 4 || bytes.data[0] != 0xff || bytes.data[1] != 0xd8) return {};
    constexpr size_t poolSize = 4096;
    std::unique_ptr<void, decltype(&std::free)> pool(allocate(poolSize), &std::free);
    if (!pool) return {};
    Decode decode{bytes, {}};
    lgfxJdec jpeg{};
    if (lgfx_jd_prepare(&jpeg, input, pool.get(), poolSize, &decode) != JDR_OK) return {};
    unsigned scale = 0;
    while ((jpeg.width >> scale) > 352 || (jpeg.height >> scale) > 256)
        if (++scale > 3) return {};
    auto& image = decode.image;
    image.width = jpeg.width >> scale;
    image.height = jpeg.height >> scale;
    if (!image.width || !image.height) return {};
    auto* pixels = static_cast<uint16_t*>(allocate(size_t(image.width) * image.height * 2));
    if (!pixels) return {};
    image.pixels = std::shared_ptr<uint16_t>(pixels, &std::free);
    return lgfx_jd_decomp(&jpeg, output, scale) == JDR_OK ? image : MediaImage{};
}
}
