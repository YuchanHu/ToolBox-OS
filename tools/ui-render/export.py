from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent.parent.parent
output = root / 'ui-preview/screenshots/lvgl'
output.mkdir(parents=True, exist_ok=True)
names = {'boot': '启动画面', 'home': '数字表盘', 'analog': '指针表盘', 'unlock-drag': '锁屏上划', 'drawer': '应用抽屉',
         'clock': '首次下拉', 'control': '控制中心', 'settings': '系统设置',
         'wifi': 'Wi-Fi 设置', 'ap': 'AP 配网', 'faces': '表盘样式', 'stopwatch': '秒表', 'stopwatch-records': '计圈记录',
         'camera-scan': '选择相机', 'camera-modes': '遥控模式', 'camera-photo': '拍照遥控', 'camera-movie': '录像遥控'}
if '--lighting' in sys.argv:
    output = root / 'output/lighting-preview'
    output.mkdir(parents=True, exist_ok=True)
    names = {'lighting-drawer': '灯光控制 · 应用抽屉', 'lighting-fixtures': '灯具列表', 'lighting-brands': '选择品牌',
             'lighting-scan': '发现灯具 · 桌面模拟', 'lighting-inkee': '影器 · 广播与配对',
             'lighting-yongnuo': '永诺 · 0.1% 与 CH', 'lighting-sidus': 'amaran P60c · 色温与 G/M',
             'lighting-fixed': 'amaran 100d · 固定色温', 'lighting-hsi': 'HSI · 度数符号'}
elif '--sony-stage8' in sys.argv:
    output = root / 'ui-preview/sony-camera/lvgl-stage8'
    output.mkdir(parents=True, exist_ok=True)
    names = {f'sony-v4-{key}': caption + ' · 桌面模拟' for key, caption in {
        'connect': '连接方式', 'pair': '配网方式', 'phone': '手机辅助', 'scan': '相机扫描',
        'key-lower': '小写键盘', 'key-upper': '大写键盘', 'key-number': '数字键盘', 'key-symbol': '符号键盘',
        'connecting': '正在连接', 'fingerprint': '核对指纹', 'changed-pin': '指纹变更', 'failed': '连接失败',
        'feedback': '反馈设置', 'stage6-console': '高级控制台', 'stage5-console': '控制台（三项）',
        'stage6-photo-entry': '拍照', 'stage6-movie-entry': '录像就绪', 'stage4-recording': '录像中',
        'stage4-photo-parameters': '拍照参数', 'stage4-movie-parameters': '录像参数', 'stage4-value': '参数编辑',
        'stage5-remote': '机身控制', 'stage5-dial': '拨轮 / 自定义键', 'stage6-latest-photo': '最新照片',
        'stage6-latest-movie': '影片回看', 'stage6-gallery': '2×2 相册', 'stage6-delete': 'RAW + JPEG 删除确认',
        'stage6-delete-failed': '删除被拒', 'stage6-empty': '空相册', 'stage7-console': '控制台（含自动化）',
        'stage7-automation': '自动化', 'stage7-interval-running': '间隔拍摄进度', 'stage7-preset': '参数预设',
        'stage7-preset-confirm': '应用预设确认', 'stage7-preset-result': '预设读回结果',
    }.items()}
elif '--sony-stage7' in sys.argv:
    output = root / 'ui-preview/sony-camera/lvgl-stage7'
    output.mkdir(parents=True, exist_ok=True)
    names = {f'sony-v4-stage7-{key}': caption + ' · 桌面模拟' for key, caption in {
        'console': '高级控制台', 'automation': '自动化', 'interval': '间隔拍摄', 'interval-running': '间隔拍摄进度',
        'preset': '参数预设', 'preset-confirm': '应用预设确认', 'preset-result': '预设读回结果',
    }.items()}
elif '--sony-stage6' in sys.argv:
    output = root / 'ui-preview/sony-camera/lvgl-stage6'
    output.mkdir(parents=True, exist_ok=True)
    names = {f'sony-v4-stage6-{key}': caption + ' · 桌面模拟' for key, caption in {
        'console': '高级控制台', 'photo-entry': '拍照入口', 'movie-entry': '录像就绪入口',
        'latest-photo': '最新照片', 'latest-movie': '影片回看', 'gallery': '相册',
        'delete': 'RAW + JPEG 删除确认', 'delete-failed': '删除被拒', 'empty': '空相册',
    }.items()}
elif '--sony-stage5' in sys.argv:
    output = root / 'ui-preview/sony-camera/lvgl-stage5'
    output.mkdir(parents=True, exist_ok=True)
    names = {f'sony-v4-stage5-{key}': caption + ' · 桌面模拟' for key, caption in {
        'console': '高级控制台', 'remote': '机身控制', 'dial': '拨轮 / 自定义键',
    }.items()}
elif '--sony-stage4' in sys.argv:
    output = root / 'ui-preview/sony-camera/lvgl-stage4'
    output.mkdir(parents=True, exist_ok=True)
    names = {f'sony-v4-stage4-{key}': caption + ' · 桌面模拟' for key, caption in {
        'console': '高级控制台', 'photo': '拍照', 'movie': '录像就绪', 'recording': '录像中',
        'photo-parameters': '拍照参数', 'movie-parameters': '录像参数', 'value': '参数编辑', 'phone': '手机辅助',
    }.items()}
elif '--sony-v4' in sys.argv:
    output = root / 'ui-preview/sony-camera/lvgl-stage3'
    output.mkdir(parents=True, exist_ok=True)
    names = {'sony-v4-connect': '连接相机', 'sony-v4-pair': '配网方式', 'sony-v4-phone': '手机辅助',
             'sony-v4-scan': '相机扫描', 'sony-v4-key-lower': '小写键盘', 'sony-v4-key-upper': '大写键盘',
             'sony-v4-key-number': '数字键盘', 'sony-v4-key-symbol': '符号键盘', 'sony-v4-connecting': '正在连接',
             'sony-v4-fingerprint': '核对指纹', 'sony-v4-changed-pin': '指纹变更',
             'sony-v4-console': '高级控制台', 'sony-v4-feedback': '反馈设置',
             'sony-v4-failed': '连接失败'}
    names = {key: value + ' · 桌面模拟' for key, value in names.items()}
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
