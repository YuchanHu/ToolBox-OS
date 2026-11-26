# ToolBox OS 1.2.0

ToolBox OS 是 M5Stack StopWatch 上的圆屏工具固件。它使用统一的 MiSans 界面和双键操作，用于计时、计数、测量和相机遥控等短时任务。普通应用随前台生命周期运行，控制中心可覆盖页面并保留当前应用。

硬件为 ESP32-S3、466×466 可视 AMOLED（CO5300）、CST820 触摸、BMI270 IMU、RX8130 RTC、KEYA/KEYB、扬声器、振动与电源管理。Wi-Fi 和 BLE 由系统统一管理。固件使用 ESP-IDF 5.5.5、FreeRTOS、LVGL 9.5.0、Mooncake 2.3.3、M5GFX 0.2.19 和 Smooth UI Toolkit。

## 应用

应用注册入口为 [app_registry.cpp](main/system/app_registry.cpp)。Launcher 管理表盘、应用抽屉和前台切换。抽屉包含以下九个应用。

| 应用 | 功能 |
| --- | --- |
| 秒表 | 启停、继续、计圈、记录与复位 |
| 设置 | 表盘、Wi-Fi、BLE、显示、声音、设备信息及电源策略 |
| 相机遥控 | Sony BLE 遥控；Wi-Fi 高级控制（拍照、录像、参数、机身按键、媒体回看与删除、间隔拍摄、参数预设） |
| 灯光控制 | 多品牌常亮灯扫描、选灯、亮度和色温控制，部分型号支持 HSI、绿洋红校正和灯效。见 [协议及验收进度](docs/LIGHTING_PROTOCOL.md) |
| 计数器 | KEYB 加一，KEYA 减一，KEYA 长按请求重置，计数保存到 NVS |
| 倒计时 | 预设、自定义时分秒、启停与到期提醒 |
| 番茄钟 | 专注、短休和长休、退出确认与完成次数 |
| 水平仪 | 双轴倾角、180° 校准、相对置零、冻结、PIN 方向与声音引导 |
| 天气 | 自动定位或指定城市、下拉刷新、逐小时趋势及最多七天预报、天气动画与离线缓存 |

## 系统操作

- 在表盘上划，或单击 KEYA 或 KEYB，进入应用抽屉。横滑切换应用，也可用 KEYA 和 KEYB 分别选择上一项和下一项。最近使用的应用排在最前，完整顺序保存在 NVS，重启后恢复。
- 从屏幕顶部下拉一次显示时间和电量，在 1.5 秒内再次下拉打开控制中心。上划关闭控制中心，回到原页面。
- 接入外部电源时，电量旁会持续显示闪电。电池充满显示 100% 后，闪电保留到断开电源。

## Sony 相机遥控

通过 BLE 扫描并连接相机，选择拍照或录像。拍照使用 KEYB。绿色外环表示录像就绪，即使相机尚未发送过录像状态，按 KEYB 也可开始录像。连接过程使用蓝色，录像中使用较厚的红色外环，保留黑色背景。

录像界面显示设备时间、电量和录像时长。时长显示延后 500 毫秒，以贴近相机计时。录像期间锁定触摸并隐藏返回按钮，按 KEYB 停止录像后即可返回。页面只在状态、计时、电量或时间发生变化时更新。

Wi-Fi 高级控制经相机 Wi-Fi Direct、SSH 指纹验证和 PTP/IP 直连 Sony 相机，无需手机或路由器。首次连接可用手机辅助或本机九宫格键盘输入凭据。功能、测试证据、刷写方法、相机侧设置和实机验收清单见 [Sony 相机阶段 8 验收报告](docs/SONY_CAMERA_STAGE8.md)。

各工具应用的按键见[五应用说明](docs/FIVE_APPS.md)。

## 架构与目录

```text
main/apps/     Launcher、九个前台应用和公共按键、计时、音频组件
main/system/   Runtime、应用注册、系统壳层、设置、共用 LVGL UI 与服务接口
main/hal/      显示、触摸、网络、传感器、音频和电源驱动
main/assets/   MiSans、图标、配网页和开屏资源
components/   固定版本的第三方组件
tools/        固件构建、字体及图标生成、同源 LVGL 桌面工具
ui-preview/   已确认的浏览器原型与 LVGL 实际截图
docs/         构建、交互、架构与性能验收记录
```

Refresh V6 通过 LVGL DIRECT 双缓冲、内部 RAM DMA 条带与 CO5300 TE 同步输出。触摸由 GPIO13 中断唤醒，按住期间以 4 ms 采样并合并移动事件。静止时以 32 ms 保底轮询，显示任务在没有帧待发时关闭 TE 中断。见 [架构](docs/ARCHITECTURE.md) 和 [性能验收](docs/PERFORMANCE.md)。

## 构建与刷写

在已导出 ESP-IDF 5.5.5 的终端执行以下命令，从串口列表选择开发板对应端口。

```bash
idf.py -B build-toolbox build
```

Windows 可运行 `./tools/build.ps1`。应用固件输出到 `build-toolbox/ToolBoxOS.bin`，完整 USB 刷写使用同目录生成的 `flash_args`。

- 已使用 1.2 单 Factory 分区的设备可运行 `idf.py -B build-toolbox -p PORT app-flash monitor` 更新应用。
- 从 1.1 升级前先备份文件和 NVS。Storage 从 `0xA00000` 移至 `0xBF0000`，请按文档步骤写入新固件并恢复 FAT。新设备也可按文档步骤初始化空卷。[迁移与 USB 刷写步骤](docs/BUILD.md)

## 界面预览

桌面渲染用 `tools/ui-render/build.cmd` 构建。在仓库根目录运行 `build-ui-render/render.exe`，再运行 `python tools/ui-render/export.py` 和 `python tools/ui-render/export_extensions.py` 导出截图。构建依赖、字体生成方法和输出路径见[构建指南](docs/BUILD.md)。

- [系统同源 LVGL 截图](ui-preview/screenshots/lvgl/overview.png)
- [应用抽屉](ui-preview/screenshots/lvgl/drawer.png)
- [拍照遥控](ui-preview/screenshots/lvgl/camera-photo.png)和[录像遥控](ui-preview/screenshots/lvgl/camera-movie.png)
- [Sony Wi-Fi 高级控制四宫格](ui-preview/sony-camera/lvgl-stage8/sony-v4-stage6-console.png)与[灯控桌面预览](output/lighting-preview/overview.png)
- [五应用同源 LVGL 截图](ui-preview/five-apps/actual/overview.png)
- [已确认浏览器原型](ui-preview/index.html)

## 验证与许可

1.2 已在 StopWatch 上完成 USB 分区迁移并挂载恢复的 FAT。构建结果、八个应用的测试范围及仍需手动验证的项目见 [1.2 整改报告](docs/REMEDIATION-1.2.md)。显示采样方法及结果见 [性能验收](docs/PERFORMANCE.md)。

项目许可见 [LICENSE](LICENSE)。第三方组件沿用各自目录内的版权和许可文件。固定版本及补丁见 [repos.json](repos.json)、[dependencies.lock](dependencies.lock) 和 [patches](patches/)。界面使用小米 MiSans 字体，许可见 [MiSans 官方下载与协议](https://hyperos.mi.com/font/download)。官方固件参考起点见 [开发记录](docs/CHANGELOG.md)。
