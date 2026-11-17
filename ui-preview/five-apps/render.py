"""UI-only review boards. Coordinates and colors follow main/system/ui.cpp.
No firmware registration, measurement, network or timer business code.
"""
from pathlib import Path
from math import sin, cos, radians
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
ACCENT = '#9dc6ff'
WHITE = '#f3f6fc'
MUTED = '#98a4b7'

def font(size, bold=False):
    return ImageFont.truetype(str(ROOT / 'reference/MiSans/ttf' / ('MiSans-Semibold.ttf' if bold else 'MiSans-Regular.ttf')), size)

def text(value, y, size=23, color=WHITE, x=233, bold=False):
    d.text((x, y), value, font=font(size, bold), fill=color, anchor='mm')

def button(value, x, y, w=130, h=61, active=False):
    d.rounded_rectangle((x,y,x+w,y+h), radius=24, fill=ACCENT if active else '#202631')
    text(value,y+h/2,23,'#081627' if active else WHITE,x+w/2)

def base(title, back=True):
    global im,d
    im=Image.new('RGB',(466,466),'black'); d=ImageDraw.Draw(im)
    text(title,61,26,bold=True)
    if not back: return
    d.rounded_rectangle((63,82,109,128),radius=23,fill='#202631')
    d.line([(90,96),(82,105),(90,114)],fill=ACCENT,width=2)

def settings(title, rows):
    base(title)
    for i,(name,value) in enumerate(rows):
        y=144+i*64
        d.rounded_rectangle((69,y,397,y+58),radius=22,fill='#181d25')
        d.text((88,y+31),name,font=font(23),fill=WHITE,anchor='lm')
        d.text((378,y+31),value,font=font(20),fill=ACCENT,anchor='rm')

def save(name):
    mask=Image.new('L',(466,466)); ImageDraw.Draw(mask).ellipse((0,0,465,465),fill=255)
    im.putalpha(mask); im.save(OUT/(name+'.png'))

def ring(progress):
    d.arc((3,3,462,462),0,360,fill='#202631',width=5)
    d.arc((3,3,462,462),-90,-90+360*progress,fill=ACCENT,width=5)

def dialog(title, message, confirm):
    # Existing badge dialog geometry; public ToolBox palette and buttons.
    overlay=Image.new('RGBA',im.size,(0,0,0,165)); im.paste(overlay,(0,0),overlay)
    d.rounded_rectangle((31,132,435,334),radius=58,fill='#2b2b2b',outline='#6a6a6a',width=3)
    text(title,171,26)
    if message: text(message,215,20)
    button('取消',69 if confirm else 159,254,148,63)
    if confirm: button(confirm,249,254,148,63,True)

boards={}
def board(group, entries):
    sheet=Image.new('RGB',(1000,540*((len(entries)+1)//2)),'#101216'); draw=ImageDraw.Draw(sheet)
    for i,(name,label) in enumerate(entries):
        img=Image.open(OUT/(name+'.png'))
        x=17+(i%2)*500; y=(i//2)*540
        draw.text((x+12,y+16),label,font=font(20),fill='#adb7c7')
        sheet.paste(img,(x,y+52),img)
    sheet.save(OUT/(group+'.png')); boards[group]=entries

entries=[]
for name,value,stage,progress,action in [('focus-ready','25:00','第 1 / 4 轮',1,'开始'),('focus-run','18:42','专注 · 第 1 / 4 轮',.75,''),('focus-rest','05:00','短休息',1,'开始休息')]:
    base('番茄钟', False); ring(progress); text(value,225,65); text(stage,290,20,ACCENT)
    save(name+'-idle')
    if action: button(action,143,350,180,48,True)
    else: button('退出专注',143,350,180,48)
    button('设置',163,405,140,42); save(name); entries.extend([(name+'-idle',stage+' · 默认'),(name,stage+' · 点击显示')])
base('番茄钟'); ring(.75); text('18:42',225,65); dialog('退出专注？','再次按 KEYA 退出专注',''); save('focus-confirm'); entries.append(('focus-confirm','退出确认 · 计时继续'))
settings('专注设置',[('专注','25 分钟'),('短休息','5 分钟'),('长休息','15 分钟'),('完成统计','查看 ›')]); save('focus-settings'); entries.append(('focus-settings','设置与统计入口'))
board('Focus',entries)

base('倒计时')
for i,v in enumerate(['1 分钟','3 分钟','5 分钟','10 分钟','15 分钟','30 分钟']): button(v,69+i%2*171,144+i//2*72,157,60)
button('自定义',151,372,164,48,True); save('timers-presets')
entries=[('timers-presets','常用预设')]
for name,value,status,action,p in [('timers-run','06:32','倒计时','暂停',.65),('timers-pause','06:32','已暂停','继续',.65),('timers-done','00:00','已完成','再来一次',0)]:
    base(status, False); ring(p); text(value,225,65); text('10 分钟',290,20,ACCENT); save(name+'-idle'); button('重置',94,350,130,48); button(action,240,350,130,48,True);save(name);entries.extend([(name+'-idle',status+' · 默认'),(name,status+' · 点击显示')])
base('自定义时间');text('时          分          秒',162,20,MUTED)
for i,v in enumerate(['00','10','00']):
    x=77+108*i; button('+',x,186,96,40);text(v,270,36,x=x+48);button('−',x,306,96,40)
button('保存常用',94,365,130,48);button('使用',240,365,130,48,True);save('timers-custom');entries.append(('timers-custom','自定义时分秒'))
board('Timers',entries)

base('计数器', False);text('128',225,98,bold=True);save('counter-main')
dialog('确定重置计数？','','确定重置');save('counter-confirm')
board('Counter',[('counter-main','计数'),('counter-confirm','长按 KEYA · 屏幕确认清零')])

entries=[]
for name,day in [('weather-yesterday','昨日'),('weather-today','今日'),('weather-tomorrow','明日')]:
    base('天气');text('选择城市',159,23,ACCENT);text(day,198,20,MUTED);text('--°',253,65);text('暂无数据',306,23);text('最高 --°   最低 --°',347,20,MUTED);text('降水概率 --%',378,20,MUTED);button('刷新',163,408,140,42);save(name);entries.append((name,day+' · 数据占位'))
settings('天气详情',[('体感温度','--°'),('湿度','--%'),('风速','-- km/h'),('更新时间','--')]);save('weather-details');entries.append(('weather-details','详情 · 数据占位'))
base('选择城市');button('城市名称 / 拼音',69,144,328,54);text('输入城市后搜索',247,23,MUTED);button('搜索',151,344,164,54,True);save('weather-city');entries.append(('weather-city','城市搜索入口'))
board('Weather',entries)

entries=[]
for name,value,bg,caption in [('level-live','02.35°','#050505','二维倾斜'),('level-zero','00.00°','#168e49','水平达标'),('level-pin','12.00°','#185fc8','PIN 达标'),('level-hold','02.35°','#050505','冻结读数')]:
    base('');d.ellipse((15,15,451,451),fill=bg)
    for a in range(0,360,5):
        r=199 if a%30==0 else 205
        d.line([(233+r*cos(radians(a)),233+r*sin(radians(a))),(233+214*cos(radians(a)),233+214*sin(radians(a)))],fill='#aab5ba',width=2)
    d.arc((62,62,404,404),205,335,fill='white',width=7)
    d.arc((62,62,404,404),25,155,fill='white',width=7)
    text('PIN 12.00°' if 'pin' in name else 'HOLD' if 'hold' in name else 'PINPOINT',124,23)
    text(value,232,65,bold=True)
    if name=='level-live':
        d.polygon([(346,133),(327,139),(341,153)],fill='white')
        text('X +1.41°   Y −1.88°',305,20)
    else: text(caption,305,23)
    save(name+'-idle')
    button('‹',106,361,58,46);button('设置',180,361,106,46);button('声音',296,361,70,46)
    save(name);entries.extend([(name+'-idle',caption+' · 默认'),(name,caption+' · 点击显示')])
settings('水平仪设置',[('提示音','开启'),('容差','0.10°'),('相对零点','清除'),('校准','进入 ›')]);save('level-settings');entries.append(('level-settings','ToolBox 设置页'))
base('翻转校准');text('屏幕朝上，保持静止',175,23);text('180°',251,36,ACCENT);text('原地旋转后采集第二次',309,20,MUTED);button('采集',143,358,180,61,True);save('level-calibration');entries.append(('level-calibration','180° 辅助校准'))
board('Level',entries)


# The interactive preview is maintained in interaction.html.
html='<meta charset="utf-8"><meta http-equiv="refresh" content="0;url=interaction.html">'
(OUT/'index.html').write_text(html,encoding='utf-8')
