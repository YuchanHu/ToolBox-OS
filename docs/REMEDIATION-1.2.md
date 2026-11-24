# ToolBox OS 1.2 整改与验收

基线为 GitHub main 的 `67da0d3230bc864e243132fc241ed16a92ecb770`，与本机 HEAD 相同。升级前请先备份配置和文件。[USB 迁移与恢复步骤](BUILD.md)

| 阶段 | 1.1 源码核对 | 1.2 结果 |
| --- | --- | --- |
| A · 分区、依赖、持久化 | 双 OTA 槽，锁文件带开发机路径，单独擦除 NVS 未标记提交 | 单 Factory 完整 USB 迁移、跨盘干净构建和擦除后硬复位均已验证 |
| B · 显示、表盘按键 | V6 已有 DIRECT、TE 和解锁动画；表盘按键缺少同一入口，指针变化会重新绘制解锁快照 | A/B 接入已有动画，桌面覆盖触摸与按键路径，实机测量快照开销 |
| C · 天气、AP 配网 | 两天预报、固定时区、小时降水字段及六位城市校验与 UAPI 实际返回不符 | 三种查询模式、离线缓存、三个主要页面、原背景动画和下拉刷新已接入；AP 页面保存结果区分 Wi-Fi 与城市。实际手机首次配网待验 |
| D · 清理、频谱、HAL | 七个历史 App、重复参考树和字体副本；普通音频初始化频谱；Canvas 接口暴露空对象 | 核对引用后移除对应代码、依赖和资源，保留八个正式 App、硬件驱动和字体生成输入 |
| E · 回归 | 设备按旧分区运行，有既有 Wi-Fi、天气和 FAT 数据 | 完成实机迁移、天气 HTTPS、缓存读回和八个应用开关；物理交互待验项目见回归表 |

## 构建、分区与资源

| 项目 | 1.1 Refresh V6 | 1.2 发布构建 |
| --- | ---: | ---: |
| 应用镜像 | 4,887,648 B | 4,867,072 B |
| 单个可用应用分区 | 5,177,344 B | 12,386,304 B |
| 镜像余量 | 289,696 B | 7,519,232 B |
| 天气动画单次定时失效上界 | 217,156 px | 52,192 px |

ESP-IDF 5.5.5 已校验 16 MiB 分区。USB 写入清单包含 bootloader、分区表与 Factory 镜像。NVS 保持 0x9000，PHY 保持 0xF000；FAT 移至 0xBF0000，Core Dump 移至 0xFF0000。原发布配置禁用了 Flash Core Dump，1.2 启用 ELF Flash Core Dump。为避免破坏数据，FAT 挂载失败仅记录错误。

在 C 盘新目录从本地 Git 仓库克隆，叠加最终源码，运行 fetch_repos.py，并在无原构建目录、无原 sdkconfig 的情况下完成 idf.py build。该产物已刷入 COM3，并封装为 ToolBoxOS-1.2.0-StopWatch.zip，含 bootloader、分区表、Factory 镜像、flash_args 和迁移指南。原 F 盘旧生成配置已备份。按相同发布配置重建后，ESP-IDF 导出的 Kconfig 全部一致；F 盘应用产物为 4,866,992 B，比表中 C 盘交付产物小 80 B。已验证跨路径可构建，未验证字节级相同。两个路径共用已安装的编译器与本机组件缓存。

依赖锁由 Component Manager 生成，本地 Wi-Fi 组件由 IDF 的 components 搜索路径发现。上游引用固定为提交号，必需补丁以正向或反向检查区分待应用、已应用与失败。既有上游初始化与 LVGL 枚举告警仍见于日志。

COM3 的旧分区表、16 KiB NVS 及 4 MiB FAT 已分别备份。USB 写入 bootloader、分区表、Factory 固件后，在新地址恢复 FAT；设备重新挂载文件系统，并用已有 Wi-Fi 配置获得 IPv4。初次启动暴露 Wi-Fi 扫描期间系统事件任务栈溢出。修复将事件任务栈从 2,304 B 调整至 6,144 B，同时减少扫描回调中的 AP 记录拷贝。新固件读出了该次故障在 Core Dump 分区留下的有效转储。

验收结束后写回关闭采样的干净构建镜像，55 秒启动观察仅出现一次主动复位，FAT 挂载及 Wi-Fi 连接成功。进入系统时内部堆可用 127,599 B、PSRAM 可用 7,396,976 B。启动有一条 I²C 端口尚未初始化日志，对应上游 i2c_bus 创建设备总线前查询已有句柄。此次另记录一次移除 I²C 设备失败。

## 显示与输入

后续复位复查中，USB 串口控制线曾让设备进入 ROM 下载模式。使用 esptool 的 ESP32-S3 看门狗复位后，串口记录从 0x20000 加载应用、FAT 挂载成功并进入 1.2 ready。该次记录中未再出现移除 I²C 设备失败，长期传感器与触摸共用总线仍待实测。

V6 DIRECT 双缓冲、TE 同步、8 行 DMA 条带及触摸采样设置沿用基线。已锁定且空闲时跳过重复锁屏处理。表盘单击 KEYA 或 KEYB 与上划共用解锁动画；滑动、回弹或显示控制面板时会拦截重复触发。表盘时钟更新后从已绘制的 RGB565 帧更新解锁快照。桌面 DIRECT 和 FULL 模式均逐像素校验了变化后的电量在动画首帧中保持一致，并覆盖拖动、回弹、上甩、连续按键、抽屉吸附和控制中心。

同板多云天气页的脏区从 217,156 px 降至 52,192 px，平均绘制耗时从 146.03 ms 降至 80.43 ms，面板提交平均间隔从 169.06 ms 降至 97.28 ms。完整条件、CPU、堆和 DMA 数据见 [性能实测](PERFORMANCE.md)。

同一块板上，指针表盘每秒更新解锁快照的平均耗时从 403.05 ms 降至 21.91 ms。两次采样均在返回表盘并稳定后取得。

## 天气与手机配置

| 2026-11-19 匿名 HTTPS 请求 | HTTP | 响应字节 | 服务端天数 / 小时数 |
| --- | ---: | ---: | ---: |
| city=北京 | 200 | 5,188 | 7 / 24 |
| adcode=110000 | 200 | 5,188 | 7 / 24 |
| city=Tokyo | 200 | 6,504 | 3 / 24 |
| 省略 city 和 adcode | 200 | 5,183 | 7 / 24 |

Tokyo 缺少 adcode，温度带小数。IP 模式由开发机出口定位到郑州市金水区，设备实际出口需另测。国内样本部分小时缺少 pop 与 precip。Tokyo 返回了当地当天零时起的小时数组；另一次 13:30 的回归样本只保留 14:00 至 23:00 的十条未来记录。解析以服务端当地报告时间比较，优先使用 weather_icon，读取 alerts 与污染物分项。手机仅向设备 AP 提交，设备保存查询模式和原始值。旧 weather/city JSON 的 id 兼容为 adcode，新自动模式会在每次请求时省略位置参数。

主页下拉松手刷新、设备城市选择、温度趋势、逐日范围条均接入 LVGL。天气卡片可打开详情。定时刷新限于天气前台，每两小时一次；近期成功缓存可在重开时复用。网络任务使用证书包，退出标记取消并释放页面。缓存预算为每条最多 3,500 字节，小时记录只保留时间、温度和两项降水数据；超过预算优先保留每日预报。NVS 写入失败保留已有记录并记日志。

本次仅请求 extended、forecast、hourly。官网接口页将部分扩展列为 Pro，本次出口未带 Key 仍成功。官方 FAQ 列出访客 4 QPS、天气每次 2 积分；接口页宣传免费，实际额度以服务端规则为准。固件不购买资源包，HTTP 错误时保留缓存。[UAPI 接口](https://uapis.cn/docs/api-reference/get-misc-weather) · [官方 FAQ](https://uapis.cn/docs/api-reference/faq)

实机从旧 NVS 恢复郑州市的 adcode 410108。连续使用其他七个 App 后，首次测试建立 TLS 失败，当时内部最大连续块仅 12 KiB，TLS 接收缓冲为 16 KiB。将 mbedTLS 分配改为使用现有 PSRAM 后，同样顺序复测获得 HTTP 200、5,162 字节。传输层记录过一次读取错误，客户端最终返回 ESP_OK。重启后从 Flash 读回了 3,026 字节有效天气缓存，包含原查询模式、郑州市、七天和 24 条小时记录。

[三个页面的 HTML 预览](../ui-preview/weather-1.2.html) · [同源 LVGL 抓帧](../ui-preview/weather-1.2-lvgl.png)

## 回归记录

通过设备 Launcher 和各 App 的生产生命周期逐个打开并关闭全部八个 App。串口记录均确认接受打开、进入运行态以及退出后回到休眠态。下表列出进一步覆盖的功能和仍需实际操作的项目。

| 对象 | 已验证 | 待实机交互 |
| --- | --- | --- |
| 秒表 | 桌面实际按键路径、记录与界面 | 按键触感 |
| 设置 | 系统壳层、Wi-Fi / BLE / 设置页及滑块界面 | 手机配网、重启恢复、无线共存 |
| 相机遥控 | Sony GATT 模拟认证、订阅、拍照、录像、超时、断开及退出 | 连接真实相机 |
| 计数器 | 桌面计数、保存、重置与退出 | 断电保持 |
| 倒计时 | 设置、暂停恢复与结束事件 | 到期声音和振动 |
| 番茄钟 | 四轮周期、长休息、确认退出 | 音频与振动 |
| 水平仪 | 倾角、PIN 和校准模型 | 噪声、方位与实际声音 |
| 天气 | 北京与 Tokyo 真实响应解析、日期边界、按键、空状态和趋势绘制；设备 TLS、旧 adcode 和重启后缓存 | 设备出口 IP 切换、断网时连续退出重开、下拉手感 |
| NVS Settings | 桌面验证提交次数与缺失键；实机分别删除单键及清空另一命名空间，关闭并硬复位后检查三键不存在、保留键正确 | 已覆盖重启持久性 |

固件构建、桌面 DIRECT 与 FULL、独立 camera_backend_check 均通过。频谱唯一调用方是未编入正式固件的 FFT App，已删除该 App、HAL 分析状态和 esp-dsp 依赖。仅 FFT 主要静态数组就有 9,028 字节（按 256 个 int16 跳步采样、512 点时域和窗函数、1024 个 float FFT 元素及频带数组计算）；普通录音、播放和提示音路径保留。失效的 Canvas/Sprite 公共接口及空对象已移除。

## 清理与文件清单

源文件与资源按 Git 基线对象和当前同口径工作文件计，排除 .git、构建产物、依赖缓存和本报告自身。基线 675,449,957 B，当前 419,994,261 B，约减少 243.6 MiB。删除清单含 228 个跟踪文件，修改与新增清单含 45 个文件。保留字体生成的 TTF、预览字体、授权及第三方声明。两个历史参考树的 156 个受跟踪文件在统一换行后逐个相同，保留 M5StopWatch-UserDemo-main；upstream-baseline 的许可证和第三方声明保留。未改写 Git 历史。

### 修改与新增

- `CMakeLists.txt`
- `README.md`
- `components/esp-wifi-connect/CMakeLists.txt`
- `components/esp-wifi-connect/include/ssid_manager.h`
- `components/esp-wifi-connect/include/wifi_configuration_ap.h`
- `components/esp-wifi-connect/ssid_manager.cc`
- `components/esp-wifi-connect/wifi_configuration_ap.cc`
- `components/esp-wifi-connect/wifi_station.cc`
- `dependencies.lock`
- `docs/ARCHITECTURE.md`
- `docs/BUILD.md`
- `docs/FIVE_APPS.md`
- `docs/PERFORMANCE.md`
- `docs/REMEDIATION-1.2.md`
- `fetch_repos.py`
- `main/CMakeLists.txt`
- `main/apps/app_launcher/app_launcher.cpp`
- `main/apps/app_weather/app_weather.cpp`
- `main/assets/provision.html`
- `main/hal/hal.h`
- `main/hal/hal_audio.cpp`
- `main/hal/hal_display.cpp`
- `main/hal/utils/settings/settings.cc`
- `main/hal/utils/settings/settings.h`
- `main/hal/utils/wear_levelling/wear_levelling.c`
- `main/idf_component.yml`
- `main/system/extension_ui.cpp`
- `main/system/extension_ui.h`
- `main/system/runtime.cpp`
- `main/system/shell.cpp`
- `main/system/shell.h`
- `main/system/weather_decode.cpp`
- `main/system/weather_decode.h`
- `main/system/weather_service.cpp`
- `main/system/weather_service.h`
- `partitions.csv`
- `repos.json`
- `sdkconfig.defaults`
- `tools/ui-render/CMakeLists.txt`
- `tools/ui-render/extensions.cpp`
- `tools/ui-render/fixtures/weather-tokyo.json`
- `tools/ui-render/render.cpp`
- `ui-preview/design-tokens.md`
- `ui-preview/weather-1.2-lvgl.png`
- `ui-preview/weather-1.2.html`

### 删除

- `components/esp-wifi-connect/assets/wifi_configuration.html`
- `main/apps/app_alarm_clock/app_alarm_clock.cpp`
- `main/apps/app_alarm_clock/app_alarm_clock.h`
- `main/apps/app_alarm_clock/model/alarm_clock.cpp`
- `main/apps/app_alarm_clock/model/alarm_clock.h`
- `main/apps/app_alarm_clock/view/add_alarm.cpp`
- `main/apps/app_alarm_clock/view/alarm_list.cpp`
- `main/apps/app_alarm_clock/view/trigger_alarm.cpp`
- `main/apps/app_alarm_clock/view/view.h`
- `main/apps/app_badge/app_badge.cpp`
- `main/apps/app_badge/app_badge.h`
- `main/apps/app_badge/view/badge.cpp`
- `main/apps/app_badge/view/view.h`
- `main/apps/app_fft/app_fft.cpp`
- `main/apps/app_fft/app_fft.h`
- `main/apps/app_fft/view/view.cpp`
- `main/apps/app_fft/view/view.h`
- `main/apps/app_fluidbox/FluidBox.cpp`
- `main/apps/app_fluidbox/FluidBox.h`
- `main/apps/app_fluidbox/FluidBoxConfig.h`
- `main/apps/app_fluidbox/app_fluidbox.cpp`
- `main/apps/app_fluidbox/app_fluidbox.h`
- `main/apps/app_lucky_wheel/app_lucky_wheel.cpp`
- `main/apps/app_lucky_wheel/app_lucky_wheel.h`
- `main/apps/app_lucky_wheel/view/selection.cpp`
- `main/apps/app_lucky_wheel/view/view.h`
- `main/apps/app_lucky_wheel/view/wheel.cpp`
- `main/apps/app_template/app_template.cpp`
- `main/apps/app_template/app_template.h`
- `main/apps/app_watch_face/app_watch_face.cpp`
- `main/apps/app_watch_face/app_watch_face.h`
- `main/apps/app_watch_face/view/big_number.cpp`
- `main/apps/app_watch_face/view/classic.cpp`
- `main/apps/app_watch_face/view/number_flow.cpp`
- `main/apps/app_watch_face/view/simple.cpp`
- `main/apps/app_watch_face/view/watch_face.h`
- `main/apps/app_watch_face/view/watch_face_manager.cpp`
- `reference/MiSans/otf/MiSans-Bold.otf`
- `reference/MiSans/otf/MiSans-Demibold.otf`
- `reference/MiSans/otf/MiSans-ExtraLight.otf`
- `reference/MiSans/otf/MiSans-Heavy.otf`
- `reference/MiSans/otf/MiSans-Light.otf`
- `reference/MiSans/otf/MiSans-Medium.otf`
- `reference/MiSans/otf/MiSans-Normal.otf`
- `reference/MiSans/otf/MiSans-Regular.otf`
- `reference/MiSans/otf/MiSans-Semibold.otf`
- `reference/MiSans/otf/MiSans-Thin.otf`
- `reference/MiSans/ttf/MiSans-Bold.ttf`
- `reference/MiSans/ttf/MiSans-Demibold.ttf`
- `reference/MiSans/ttf/MiSans-ExtraLight.ttf`
- `reference/MiSans/ttf/MiSans-Heavy.ttf`
- `reference/MiSans/ttf/MiSans-Light.ttf`
- `reference/MiSans/ttf/MiSans-Medium.ttf`
- `reference/MiSans/ttf/MiSans-Normal.ttf`
- `reference/MiSans/ttf/MiSans-Thin.ttf`
- `reference/MiSans/woff/MiSans-Bold.woff`
- `reference/MiSans/woff/MiSans-Demibold.woff`
- `reference/MiSans/woff/MiSans-ExtraLight.woff`
- `reference/MiSans/woff/MiSans-Heavy.woff`
- `reference/MiSans/woff/MiSans-Light.woff`
- `reference/MiSans/woff/MiSans-Medium.woff`
- `reference/MiSans/woff/MiSans-Normal.woff`
- `reference/MiSans/woff/MiSans-Regular.woff`
- `reference/MiSans/woff/MiSans-Semibold.woff`
- `reference/MiSans/woff/MiSans-Thin.woff`
- `reference/MiSans/woff2/MiSans-Bold.woff2`
- `reference/MiSans/woff2/MiSans-Demibold.woff2`
- `reference/MiSans/woff2/MiSans-ExtraLight.woff2`
- `reference/MiSans/woff2/MiSans-Heavy.woff2`
- `reference/MiSans/woff2/MiSans-Light.woff2`
- `reference/MiSans/woff2/MiSans-Medium.woff2`
- `reference/MiSans/woff2/MiSans-Normal.woff2`
- `reference/MiSans/woff2/MiSans-Thin.woff2`
- `reference/MiSans/可变字体/MiSansVF.ttf`
- `reference/upstream-baseline/.clang-format`
- `reference/upstream-baseline/.github/ISSUE_TEMPLATE/bug-report.yml`
- `reference/upstream-baseline/.github/workflows/clang-format-check.yml`
- `reference/upstream-baseline/.github/workflows/wecom-digest-forward.yml`
- `reference/upstream-baseline/.gitignore`
- `reference/upstream-baseline/CMakeLists.txt`
- `reference/upstream-baseline/README.md`
- `reference/upstream-baseline/dependencies.lock`
- `reference/upstream-baseline/fetch_repos.py`
- `reference/upstream-baseline/main/CMakeLists.txt`
- `reference/upstream-baseline/main/apps/app_alarm_clock/app_alarm_clock.cpp`
- `reference/upstream-baseline/main/apps/app_alarm_clock/app_alarm_clock.h`
- `reference/upstream-baseline/main/apps/app_alarm_clock/model/alarm_clock.cpp`
- `reference/upstream-baseline/main/apps/app_alarm_clock/model/alarm_clock.h`
- `reference/upstream-baseline/main/apps/app_alarm_clock/view/add_alarm.cpp`
- `reference/upstream-baseline/main/apps/app_alarm_clock/view/alarm_list.cpp`
- `reference/upstream-baseline/main/apps/app_alarm_clock/view/trigger_alarm.cpp`
- `reference/upstream-baseline/main/apps/app_alarm_clock/view/view.h`
- `reference/upstream-baseline/main/apps/app_badge/app_badge.cpp`
- `reference/upstream-baseline/main/apps/app_badge/app_badge.h`
- `reference/upstream-baseline/main/apps/app_badge/view/badge.cpp`
- `reference/upstream-baseline/main/apps/app_badge/view/view.h`
- `reference/upstream-baseline/main/apps/app_fft/app_fft.cpp`
- `reference/upstream-baseline/main/apps/app_fft/app_fft.h`
- `reference/upstream-baseline/main/apps/app_fft/view/view.cpp`
- `reference/upstream-baseline/main/apps/app_fft/view/view.h`
- `reference/upstream-baseline/main/apps/app_fluidbox/FluidBox.cpp`
- `reference/upstream-baseline/main/apps/app_fluidbox/FluidBox.h`
- `reference/upstream-baseline/main/apps/app_fluidbox/FluidBoxConfig.h`
- `reference/upstream-baseline/main/apps/app_fluidbox/app_fluidbox.cpp`
- `reference/upstream-baseline/main/apps/app_fluidbox/app_fluidbox.h`
- `reference/upstream-baseline/main/apps/app_imu/app_imu.cpp`
- `reference/upstream-baseline/main/apps/app_imu/app_imu.h`
- `reference/upstream-baseline/main/apps/app_imu/view/view.cpp`
- `reference/upstream-baseline/main/apps/app_imu/view/view.h`
- `reference/upstream-baseline/main/apps/app_launcher/app_launcher.cpp`
- `reference/upstream-baseline/main/apps/app_launcher/app_launcher.h`
- `reference/upstream-baseline/main/apps/app_launcher/view/guide_page.cpp`
- `reference/upstream-baseline/main/apps/app_launcher/view/view.cpp`
- `reference/upstream-baseline/main/apps/app_launcher/view/view.h`
- `reference/upstream-baseline/main/apps/app_lucky_wheel/app_lucky_wheel.cpp`
- `reference/upstream-baseline/main/apps/app_lucky_wheel/app_lucky_wheel.h`
- `reference/upstream-baseline/main/apps/app_lucky_wheel/view/selection.cpp`
- `reference/upstream-baseline/main/apps/app_lucky_wheel/view/view.h`
- `reference/upstream-baseline/main/apps/app_lucky_wheel/view/wheel.cpp`
- `reference/upstream-baseline/main/apps/app_setup/app_setup.cpp`
- `reference/upstream-baseline/main/apps/app_setup/app_setup.h`
- `reference/upstream-baseline/main/apps/app_setup/view/view.cpp`
- `reference/upstream-baseline/main/apps/app_setup/view/view.h`
- `reference/upstream-baseline/main/apps/app_setup/workers/about.cpp`
- `reference/upstream-baseline/main/apps/app_setup/workers/datetime.cpp`
- `reference/upstream-baseline/main/apps/app_setup/workers/device.cpp`
- `reference/upstream-baseline/main/apps/app_setup/workers/workers.h`
- `reference/upstream-baseline/main/apps/app_stopwatch/app_stopwatch.cpp`
- `reference/upstream-baseline/main/apps/app_stopwatch/app_stopwatch.h`
- `reference/upstream-baseline/main/apps/app_stopwatch/model/stopwatch.cpp`
- `reference/upstream-baseline/main/apps/app_stopwatch/model/stopwatch.h`
- `reference/upstream-baseline/main/apps/app_stopwatch/view/view.cpp`
- `reference/upstream-baseline/main/apps/app_stopwatch/view/view.h`
- `reference/upstream-baseline/main/apps/app_template/app_template.cpp`
- `reference/upstream-baseline/main/apps/app_template/app_template.h`
- `reference/upstream-baseline/main/apps/app_watch_face/app_watch_face.cpp`
- `reference/upstream-baseline/main/apps/app_watch_face/app_watch_face.h`
- `reference/upstream-baseline/main/apps/app_watch_face/view/big_number.cpp`
- `reference/upstream-baseline/main/apps/app_watch_face/view/classic.cpp`
- `reference/upstream-baseline/main/apps/app_watch_face/view/number_flow.cpp`
- `reference/upstream-baseline/main/apps/app_watch_face/view/simple.cpp`
- `reference/upstream-baseline/main/apps/app_watch_face/view/watch_face.h`
- `reference/upstream-baseline/main/apps/app_watch_face/view/watch_face_manager.cpp`
- `reference/upstream-baseline/main/apps/apps.h`
- `reference/upstream-baseline/main/apps/common/arc_top_clock/arc_top_clock.cpp`
- `reference/upstream-baseline/main/apps/common/arc_top_clock/arc_top_clock.h`
- `reference/upstream-baseline/main/apps/common/audio/audio.cpp`
- `reference/upstream-baseline/main/apps/common/audio/audio.h`
- `reference/upstream-baseline/main/apps/common/audio/common.h`
- `reference/upstream-baseline/main/apps/common/common.h`
- `reference/upstream-baseline/main/apps/common/key_manager/key_manager.cpp`
- `reference/upstream-baseline/main/apps/common/key_manager/key_manager.h`
- `reference/upstream-baseline/main/apps/common/loading_page/loading_page.h`
- `reference/upstream-baseline/main/apps/common/status_bar/status_bar.cpp`
- `reference/upstream-baseline/main/apps/common/status_bar/status_bar.h`
- `reference/upstream-baseline/main/assets/assets.h`
- `reference/upstream-baseline/main/assets/fonts/CommissionerMedium108.c`
- `reference/upstream-baseline/main/assets/fonts/CommissionerMedium64.c`
- `reference/upstream-baseline/main/assets/fonts/MontserratSemiBold26.c`
- `reference/upstream-baseline/main/assets/fonts/lv_font_maple_mono_medium_24.c`
- `reference/upstream-baseline/main/assets/fonts/lv_font_maple_mono_medium_28.c`
- `reference/upstream-baseline/main/assets/fonts/lv_font_maple_mono_medium_48.c`
- `reference/upstream-baseline/main/assets/fonts/symbols.txt`
- `reference/upstream-baseline/main/assets/images/alarm_icon.c`
- `reference/upstream-baseline/main/assets/images/fluidbox_icon.c`
- `reference/upstream-baseline/main/assets/images/go_home_guide.c`
- `reference/upstream-baseline/main/assets/images/icon_badge.c`
- `reference/upstream-baseline/main/assets/images/icon_bat_lightning.c`
- `reference/upstream-baseline/main/assets/images/icon_clock.c`
- `reference/upstream-baseline/main/assets/images/icon_edit_badge.c`
- `reference/upstream-baseline/main/assets/images/icon_fft.c`
- `reference/upstream-baseline/main/assets/images/icon_imu.c`
- `reference/upstream-baseline/main/assets/images/icon_indicator_left.c`
- `reference/upstream-baseline/main/assets/images/icon_indicator_right.c`
- `reference/upstream-baseline/main/assets/images/icon_lucky_wheel.c`
- `reference/upstream-baseline/main/assets/images/icon_setup.c`
- `reference/upstream-baseline/main/assets/images/icon_stopwatch.c`
- `reference/upstream-baseline/main/assets/images/icon_watch_face.c`
- `reference/upstream-baseline/main/assets/images/lucky_wheel_pointer.c`
- `reference/upstream-baseline/main/assets/images/watch_face/big_number/big_number_0.c`
- `reference/upstream-baseline/main/assets/images/watch_face/big_number/big_number_1.c`
- `reference/upstream-baseline/main/assets/images/watch_face/big_number/big_number_2.c`
- `reference/upstream-baseline/main/assets/images/watch_face/big_number/big_number_3.c`
- `reference/upstream-baseline/main/assets/images/watch_face/big_number/big_number_4.c`
- `reference/upstream-baseline/main/assets/images/watch_face/big_number/big_number_5.c`
- `reference/upstream-baseline/main/assets/images/watch_face/big_number/big_number_6.c`
- `reference/upstream-baseline/main/assets/images/watch_face/big_number/big_number_7.c`
- `reference/upstream-baseline/main/assets/images/watch_face/big_number/big_number_8.c`
- `reference/upstream-baseline/main/assets/images/watch_face/big_number/big_number_9.c`
- `reference/upstream-baseline/main/assets/images/watch_face/classic/classic_hour_hand.c`
- `reference/upstream-baseline/main/assets/images/watch_face/classic/classic_minute_hand.c`
- `reference/upstream-baseline/main/assets/images/watch_face/classic/classic_second_hand.c`
- `reference/upstream-baseline/main/hal/drivers/cst820/cst820.cpp`
- `reference/upstream-baseline/main/hal/drivers/cst820/cst820.h`
- `reference/upstream-baseline/main/hal/drivers/rx8130/rx8130.cpp`
- `reference/upstream-baseline/main/hal/drivers/rx8130/rx8130.h`
- `reference/upstream-baseline/main/hal/hal.cpp`
- `reference/upstream-baseline/main/hal/hal.h`
- `reference/upstream-baseline/main/hal/hal_alarm.cpp`
- `reference/upstream-baseline/main/hal/hal_audio.cpp`
- `reference/upstream-baseline/main/hal/hal_badge.cpp`
- `reference/upstream-baseline/main/hal/hal_button.cpp`
- `reference/upstream-baseline/main/hal/hal_display.cpp`
- `reference/upstream-baseline/main/hal/hal_fs.cpp`
- `reference/upstream-baseline/main/hal/hal_imu.cpp`
- `reference/upstream-baseline/main/hal/hal_ioe.cpp`
- `reference/upstream-baseline/main/hal/hal_network.cpp`
- `reference/upstream-baseline/main/hal/hal_pmic.cpp`
- `reference/upstream-baseline/main/hal/hal_rtc.cpp`
- `reference/upstream-baseline/main/hal/utils/button/Button_Class.cpp`
- `reference/upstream-baseline/main/hal/utils/button/Button_Class.hpp`
- `reference/upstream-baseline/main/hal/utils/config_ap/assets/badge_config_ap.html`
- `reference/upstream-baseline/main/hal/utils/config_ap/config_ap.cpp`
- `reference/upstream-baseline/main/hal/utils/config_ap/config_ap.h`
- `reference/upstream-baseline/main/hal/utils/settings/settings.cc`
- `reference/upstream-baseline/main/hal/utils/settings/settings.h`
- `reference/upstream-baseline/main/hal/utils/wear_levelling/wear_levelling.c`
- `reference/upstream-baseline/main/hal/utils/wear_levelling/wear_levelling.h`
- `reference/upstream-baseline/main/idf_component.yml`
- `reference/upstream-baseline/main/main.cpp`
- `reference/upstream-baseline/partitions.csv`
- `reference/upstream-baseline/patches/M5IOE1.patch`
- `reference/upstream-baseline/patches/M5PM1.patch`
- `reference/upstream-baseline/platformio.ini`
- `reference/upstream-baseline/repos.json`
- `reference/upstream-baseline/scripts/fetch_components.py`
- `reference/upstream-baseline/sdkconfig.defaults`
- `reference/upstream-baseline/sdkconfig.m5stack-stopwatch`
