"""Export production LVGL captures for comparison with the approved UI."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[2]
output = root / 'ui-preview/five-apps/actual'
output.mkdir(exist_ok=True)
names = ['counter-main', 'counter-confirm', 'timers-run', 'timers-run-controls',
         'focus-run', 'focus-run-controls', 'focus-confirm', 'level-live',
         'level-live-controls', 'level-zero', 'level-pin', 'weather-empty',
         'weather-city', 'focus-settings', 'timers-presets', 'timers-custom',
         'weather-today', 'weather-tomorrow', 'weather-sun', 'weather-night']
names += [f'weather-animation-{code}' for code in [103, 104, 501, 309, 305, 307, 313, 400, 404, 302, 304]]
font = ImageFont.truetype(str(root / 'reference/MiSans/ttf/MiSans-Regular.ttf'), 20)
mask = Image.new('L', (466, 466))
ImageDraw.Draw(mask).ellipse((0, 0, 465, 465), fill=255)
for name in names:
    source = root / f'build-ui-render/images/{name}.bgra'
    image = Image.frombytes('RGBA', (468, 466), source.read_bytes(), 'raw', 'BGRA').crop((1, 0, 467, 466))
    image.putalpha(mask)
    image.save(output / f'{name}.png')

def board(filename, entries):
    sheet = Image.new('RGB', (1506, ((len(entries) + 2) // 3) * 530), '#101216')
    draw = ImageDraw.Draw(sheet)
    for i, (name, caption) in enumerate(entries):
        image = Image.open(output / f'{name}.png')
        x, y = 18 + i % 3 * 502, i // 3 * 530
        draw.text((x, y + 10), caption, font=font, fill='#adb7c7')
        sheet.paste(image, (x, y + 44), image)
    sheet.save(output / filename)

board('overview.png', [('level-live', 'Level'), ('focus-run', 'Focus'), ('timers-run', 'Timers'),
                       ('weather-today', 'Weather · 实际接口数据'), ('counter-main', 'Counter')])
board('states.png', [(name, name) for name in names])
