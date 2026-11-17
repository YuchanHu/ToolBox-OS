"""Encode the approved 150-pixel launcher PNGs as native LVGL RGB565 assets."""
from pathlib import Path

from PIL import Image

root = Path(__file__).resolve().parent.parent
names = ("stopwatch", "settings", "camera", "counter", "timers", "focus", "level", "weather")
output = ['#include <lvgl.h>', '']
declarations = ['#pragma once', '#include <lvgl.h>', '#ifdef __cplusplus', 'extern "C" {', '#endif']
for name in names:
    image = Image.open(root / "output/app-icons" / f"{name}.png").convert("RGB")
    if image.size != (150, 150):
        raise ValueError(f"{name} must be exactly 150 x 150 pixels")
    data = bytearray()
    for red, green, blue in image.getdata():
        pixel = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        data.extend((pixel & 255, pixel >> 8))
    symbol = f"toolbox_app_{name}"
    output.append(f"static const uint8_t {symbol}_pixels[] = {{")
    for offset in range(0, len(data), 24):
        output.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset:offset + 24]) + ",")
    output.extend([
        "};",
        f"const lv_image_dsc_t {symbol} = {{",
        "    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,",
        "               .w = 150, .h = 150, .stride = 300},",
        f"    .data_size = {len(data)}, .data = {symbol}_pixels",
        "};", "",
    ])
    declarations.append(f"LV_IMAGE_DECLARE({symbol});")
declarations.extend(['#ifdef __cplusplus', '}', '#endif', ''])
(root / "main/assets/app_icons.c").write_text("\n".join(output), encoding="utf-8")
(root / "main/system/app_icons.h").write_text("\n".join(declarations), encoding="utf-8")
