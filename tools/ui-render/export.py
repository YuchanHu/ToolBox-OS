from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent.parent.parent
output = root / 'ui-preview/screenshots/lvgl'
output.mkdir(parents=True, exist_ok=True)
names = {'boot': '启动画面', 'home': '数字表盘', 'analog': '指针表盘', 'drawer': '应用抽屉',
         'clock': '首次下拉', 'control': '控制中心', 'settings': '系统设置',
         'wifi': 'Wi-Fi 设置', 'ap': 'AP 配网', 'faces': '表盘样式', 'stopwatch': '秒表', 'stopwatch-records': '计圈记录',
         'camera-scan': '选择相机', 'camera-modes': '遥控模式', 'camera-photo': '拍照遥控', 'camera-movie': '录像遥控'}
font = ImageFont.truetype(str(root / 'reference/MiSans/ttf/MiSans-Regular.ttf'), 20)
sheet = Image.new('RGB', (1506, ((len(names) + 2) // 3) * 530), '#101216')
draw = ImageDraw.Draw(sheet)
for index, (name, caption) in enumerate(names.items()):
    source = root / f'build-ui-render/images/{name}.bgra'
    image = Image.frombytes('RGBA', (468, 466), source.read_bytes(), 'raw', 'BGRA').crop((1, 0, 467, 466))
    mask = Image.new('L', (466, 466))
    ImageDraw.Draw(mask).ellipse((0, 0, 465, 465), fill=255)
    image.putalpha(mask)
    image.save(output / f'{name}.png')
    x, y = 18 + (index % 3) * 502, (index // 3) * 530
    draw.text((x + 8, y + 10), caption, font=font, fill='#adb7c7')
    sheet.paste(image, (x, y + 44), image)
sheet.save(output / 'overview.png')
