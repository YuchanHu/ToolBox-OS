# ToolBox OS 1.1

ToolBox OS 是 M5Stack StopWatch 上的圆屏工具固件。它使用统一的 MiSans 界面和双键操作，用于计时、计数、测量和相机遥控等短时任务。普通应用随前台生命周期运行，控制中心可覆盖页面并保留当前应用。

硬件为 ESP32-S3、466×466 可视 AMOLED（CO5300）、CST820 触摸、BMI270 IMU、RX8130 RTC、KEYA/KEYB、扬声器、振动与电源管理。Wi-Fi 和 BLE 由系统统一管理。固件使用 ESP-IDF 5.5.5、FreeRTOS、LVGL 9.5.0、Mooncake 2.3.3、M5GFX 0.2.19 和 Smooth UI Toolkit。

## 应用

应用注册入口为 [app_registry.cpp](main/system/app_registry.cpp)。Launcher 管理表盘、应用抽屉和前台切换。抽屉包含以下八个应用。

| 应用 | 功能 |
| --- | --- |
| 秒表 | 启停、继续、计圈、记录与复位 |
| 设置 | 表盘、Wi-Fi、BLE、显示、声音、设备信息及电源策略 |
| 相机遥控 | Sony BLE 相机扫描、连接、拍照、录像与状态反馈 |
| 计数器 | KEYB 加一，KEYA 减一，KEYA 长按请求重置，计数保存到 NVS |
| 倒计时 | 预设、自定义时分秒、启停与到期提醒 |
| 番茄钟 | 专注、短休和长休、退出确认与完成次数 |
| 水平仪 | 双轴倾角、180° 校准、相对置零、冻结、PIN 方向与声音引导 |
| 天气 | 城市搜索与保存、今日明日、天气动画与日期缓存 |

从锁屏上划进入抽屉，短距离拖动可回弹，快速上甩可解锁。抽屉横滑循环吸附，KEYA 和 KEYB 分别切换上一项和下一项。顶部第一次下拉显示时间和电量，1.5 秒内再次下拉打开控制中心。控制中心上划回到原页面。各应用的按键见 [五应用说明](docs/FIVE_APPS.md)。

## 架构与目录

```text
main/apps/     Launcher、八个前台应用和公共按键、计时、音频组件
main/system/   Runtime、应用注册、系统壳层、设置、共用 LVGL UI 与服务接口
main/hal/      显示、触摸、网络、传感器、音频和电源驱动
main/assets/   MiSans、图标、配网页和启动音频资源
components/   固定版本的第三方组件
tools/        固件构建、字体及图标生成、同源 LVGL 桌面工具
ui-preview/   已确认的浏览器原型与 LVGL 实际截图
docs/         构建、交互、架构与性能验收记录
```

Refresh V6 通过 LVGL DIRECT 双缓冲、内部 RAM DMA 条带与 CO5300 TE 同步输出。触摸每 4ms 独立采样，移动事件合并，按下、抬起和取消保序。面板繁忙时仅暂停新一轮绘制。见 [架构](docs/ARCHITECTURE.md) 和 [性能验收](docs/PERFORMANCE.md)。

## 构建和预览

在已导出 ESP-IDF 5.5.5 的终端执行以下命令，从串口列表选择开发板对应端口。

```bash
idf.py -B build-toolbox build
idf.py -B build-toolbox -p PORT flash monitor
```

Windows 可运行 `./tools/build.ps1`。桌面渲染用 `tools/ui-render/build.cmd` 构建，在仓库根目录运行 `build-ui-render/render.exe`，再运行 `python tools/ui-render/export.py` 和 `python tools/ui-render/export_extensions.py`。字体生成使用 `npm ci --prefix tools/font-converter` 和 `python tools/generate-fonts.py`。参数、依赖及输出见 [构建指南](docs/BUILD.md)。

- [系统同源 LVGL 截图](ui-preview/screenshots/lvgl/overview.png)
- [五应用同源 LVGL 截图](ui-preview/five-apps/actual/overview.png)
- [已确认浏览器原型](ui-preview/index.html)

## 验证与许可

固件交叉编译和桌面回归覆盖范围见 [性能验收](docs/PERFORMANCE.md)。V5 串口已采集单个刷新窗口。V6 的显示、触摸、无线、音频、天气网络和 IMU 实机回归及两版操作场景对比待完成。

项目许可见 [LICENSE](LICENSE)。第三方组件沿用各自目录内的版权和许可文件。固定版本及补丁见 [repos.json](repos.json)、[dependencies.lock](dependencies.lock) 和 [patches](patches/)。界面使用小米 MiSans 字体，许可见 [MiSans 官方下载与协议](https://hyperos.mi.com/font/download)。官方固件参考起点见 [开发记录](docs/CHANGELOG.md)。
