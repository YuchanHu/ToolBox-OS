from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parent.parent
declarations = ['#pragma once', '#include <lvgl.h>', '#ifdef __cplusplus', 'extern "C" {', '#endif']
output = ['#include <lvgl.h>']
for image in sorted((root / 'ui-preview/assets/icons').glob('*.png')):
    name = 'toolbox_icon_' + image.stem.replace('-', '_')
    bitmap = Image.open(image).convert('RGBA')
    data = bitmap.tobytes('raw', 'BGRA')
    output.append(f'static const uint8_t {name}_pixels[] = {{')
    for offset in range(0, len(data), 32):
        output.append(','.join(str(b) for b in data[offset:offset+32]) + ',')
    output += ['};', f'const lv_image_dsc_t {name} = {{',
               f'.header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .w = {bitmap.width}, .h = {bitmap.height}, .stride = {bitmap.width * 4}}},',
               f'.data_size = {len(data)}, .data = {name}_pixels', '};']
    declarations.append(f'LV_IMAGE_DECLARE({name});')
declarations += ['#ifdef __cplusplus', '}', '#endif']
(root / 'main/assets/ui_icons.c').write_text('\n'.join(output))
(root / 'main/system/icons.h').write_text('\n'.join(declarations))
