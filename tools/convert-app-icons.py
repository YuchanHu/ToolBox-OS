"""Encode launcher PNGs at their displayed size in the panel's RGB565 byte order."""
from pathlib import Path

from PIL import Image

root = Path(__file__).resolve().parent.parent
names = ("stopwatch", "settings", "camera", "counter", "timers", "focus", "level", "weather")
display_size = 166
output = ['#include <lvgl.h>', '']
declarations = ['#pragma once', '#include <lvgl.h>', '#ifdef __cplusplus', 'extern "C" {', '#endif']
for name in names:
    image = Image.open(root / "output/app-icons" / f"{name}.png").convert("RGB")
    if image.size != (150, 150):
        raise ValueError(f"{name} must be exactly 150 x 150 pixels")
    image = image.resize((display_size, display_size), Image.Resampling.BILINEAR)
    data = bytearray()
    for red, green, blue in image.getdata():
        pixel = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        data.extend((pixel >> 8, pixel & 255))
    symbol = f"toolbox_app_{name}"
    output.append(f"static const uint8_t {symbol}_pixels[] = {{")
    for offset in range(0, len(data), 24):
        output.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset:offset + 24]) + ",")
    output.extend([
        "};",
        f"const lv_image_dsc_t {symbol} = {{",
        "    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565_SWAPPED,",
        f"               .w = {display_size}, .h = {display_size}, .stride = {display_size * 2}}},",
        f"    .data_size = {len(data)}, .data = {symbol}_pixels",
        "};", "",
    ])
    declarations.append(f"LV_IMAGE_DECLARE({symbol});")
declarations.extend(['#ifdef __cplusplus', '}', '#endif', ''])
(root / "main/assets/app_icons.c").write_text("\n".join(output), encoding="utf-8")
(root / "main/system/app_icons.h").write_text("\n".join(declarations), encoding="utf-8")
