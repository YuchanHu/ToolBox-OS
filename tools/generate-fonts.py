"""Convert the local MiSans source into shared LVGL fonts."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
converter = root / 'tools/font-converter/node_modules/lv_font_conv/lv_font_conv.js'
characters = set('·：，。％°？‹›−~晴多云阴少阵雷雨夹雪冻冰雹暴大中小毛雾霾沙尘扬浮强热带极特')
for source in (root / 'main').rglob('*.cpp'):
    characters.update(c for c in source.read_text(encoding='utf-8') if '\u4e00' <= c <= '\u9fff')
symbols = ''.join(sorted(characters))
city_characters = set()
for first in range(0xB0, 0xF8):
    for second in range(0xA1, 0xFF):
        try:
            city_characters.add(bytes((first, second)).decode('gb2312'))
        except UnicodeDecodeError:
            pass
city_symbols = ''.join(sorted(city_characters | characters))
for size in (18, 20, 22, 23, 24, 25, 26, 28, 32, 36, 64, 65, 96, 98):
    weight = 'Semibold' if size in (26, 32, 96, 98) else 'Regular'
    command = ['node', str(converter), '--size', str(size), '--bpp', '4',
               '--format', 'lvgl', '--font', str(root / f'reference/MiSans/ttf/MiSans-{weight}.ttf'),
               '-o', str(root / f'main/assets/fonts/toolbox_misans_{size}.c')]
    if size in (65, 96, 98):
        command += ['--symbols', '0123456789:.-°~']
    elif size in (18, 22, 24, 32):
        command += ['-r', '32-126']
    else:
        command += ['-r', '32-126', '--symbols', symbols]
    if size in (20, 26):
        # Dynamic domestic city names and API weather text use the existing families.
        command[command.index('--bpp') + 1] = '2'
        command[command.index('--symbols') + 1] = city_symbols
    subprocess.run(command, check=True, cwd=root)

subprocess.run(['node', str(converter), '--size', '36', '--bpp', '4', '--format', 'lvgl',
    '--font', str(root / 'reference/MiSans/ttf/MiSans-Semibold.ttf'), '--symbols', '0123456789:',
    '-o', str(root / 'main/assets/fonts/toolbox_misans_semibold_36.c')], check=True, cwd=root)

subprocess.run(['node', str(converter), '--size', '65', '--bpp', '4', '--format', 'lvgl',
    '--font', str(root / 'reference/MiSans/ttf/MiSans-Semibold.ttf'), '--symbols', '0123456789.°-',
    '-o', str(root / 'main/assets/fonts/toolbox_misans_semibold_65.c')], check=True, cwd=root)
