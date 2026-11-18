# Refresh V6 性能与验收

## 采样方法

用相同开发板、固件优化级别、亮度、无线配置和交互脚本测试两版。每个场景先记录稳定状态，再连续操作至少 30 秒。首次锁屏上划单独采样，避免被后续已缓存的操作平均。每轮保留原始串口日志、固件版本、DMA 行数、Wi-Fi/BLE 状态和测试起止时间。

通过 `idf.py -B build-toolbox menuconfig` 在 ToolBox OS 菜单打开 `CONFIG_TOOLBOX_PERF`。FreeRTOS 同时打开 trace facility、运行时间统计并选择 ESP Timer 时，日志报告双核占用和 Runtime、LVGL、触摸、DMA 任务占用。串口通过 `idf.py -B build-toolbox -p PORT monitor` 采集 `Refresh`。发布配置关闭采样。

每个约五秒的窗口中，固定容量直方图输出样本数、均值、最大值及 P95/P99 的所在桶上界。时间为微秒。事件累计看样本数，脏区统计按像素计。活跃 FPS 使用连续的成功传输间隔，要求相邻两帧均在最近触摸后的 250ms 内。总 FPS 包含静止时间，渲染与面板提交分别计数。

| 指标 | 计算位置 |
| --- | --- |
| `touch_sample_interval`、`touch_read_time`、`touch_read_error` | CST820 轮询开始间隔、实际 I²C 调用耗时及失败 |
| `touch_queue_depth`、`touch_queue_delay`、`touch_overflow` | 非空队列深度、采样结束至消费及溢出 |
| `gui_lock_wait`、`gui_lock_hold`、`event_process_time` | LVGL 锁等待、持锁、输入读取及事件分发 |
| `snapshot_time`、`render_time`、`render_interval` | 快照、实际 LVGL 绘制段和绘制间隔 |
| `buffer_sync_time` | DIRECT 在首个 flush-wait 返回到 RENDER_START 的时间，包括区域复制与相邻的刷新开销 |
| `te_wait_time`、`te_phase`、`te_period`、`te_missed`、`te_timeout` | TE 等待耗时、ISR 记录边沿至接收的相位、TE 周期、晚唤醒和超时 |
| `dma_pack_time`、`dma_transfer_time`、`dma_bursts`、`dma_over_budget` | 条带复制耗时、提交至末段完成、条带数及超过 TE 周期的帧 |
| `dirty_pixels`、`dirty_ratio`、`transfer_pixels`、`frame_drop` | LVGL 脏区、占屏比例、对齐后传输的像素和放弃的提交 |
| `input_to_present` | 最近被消费的输入样本至随后关联 DMA 完成的时间，代表软件链路延迟 |
| `internal_heap`、`largest_internal_block`、`psram_free` | 内部 RAM、最大连续块及 PSRAM 余量 |

`te_phase` 从 ISR 记录时间起算。若要测物理 TE 边沿到中断及 QSPI 的相位，用逻辑分析仪同时采集 GPIO38、CS 和 SCLK。真实触摸到发光延迟由外部相机或光电传感器测量。串口聚合窗口以应用任务节拍发送。

## 当前实机数据

2026-11-18 通过 COM3 读到运行中 V5 的一个刷新窗口。窗口包含静止时间，未标注操作场景。

| V5 指标 | 实机串口值 |
| --- | ---: |
| 面板提交 FPS（含静止） | 1.0 |
| 传输耗时，平均 / 最大 | 6,253 / 6,712 μs |
| 打包耗时，平均 | 4,523 μs |
| TE 周期 | 16,924 μs |
| TE 边沿 / 晚唤醒 / 超时 | 297 / 0 / 0 |
| TE 相位，最大 | 11 μs |

| 场景 | V5 活跃 FPS | V6 活跃 FPS | V5 P95 输入延迟 | V6 P95 输入延迟 |
| --- | --- | --- | --- | --- |
| 锁屏上划与回弹 | 待测 | 待测 | 待测 | 待测 |
| 抽屉连续滑动 | 待测 | 待测 | 待测 | 待测 |
| 设置连续滚动 | 待测 | 待测 | 待测 | 待测 |
| 两级下拉与控制中心滑块 | 待测 | 待测 | 待测 | 待测 |
| 天气动画 | 待测 | 待测 | 待测 | 待测 |
| Wi-Fi 与 BLE 共存 | 待测 | 待测 | 待测 | 待测 |

另外记录数字表盘空闲、指针秒针、首次上划、慢速抽屉拖动、倒计时外圈和水平仪动态测量。无线条件分别覆盖关闭、仅 Wi-Fi、仅 BLE 和共存。活跃 P95 帧间隔、最长帧间隔和实际提交率应随每组对比保存。

## 硬件参数实验

| 条带行数 | 全屏段数 | 内部双缓冲 |
| ---: | ---: | ---: |
| 8 | 59 | 14,976 B |
| 16 | 30 | 29,952 B |
| 24 | 20 | 44,928 B |
| 32 | 15 | 59,904 B |

以 468×466 传输范围、2 B/px 计算。每个配置运行全屏过渡和小脏区场景，记录打包/传输、TE、CPU 和无线共存时的内部 RAM。默认 8 行。CST820 默认 100kHz，实验开关只将该设备改为 400kHz。分别采样 I²C 调用间隔与失败率，复测 RTC、IMU、供电和快速抬手。保留 240MHz CPU、80MHz PSRAM 与 80MHz QSPI 配置。优先验证 TE 撕裂和无线可用性，再选择实验参数。

## 功能回归

桌面用固件 LVGL 源码、图标和字形，HAL 使用测试输入。验证锁屏、回弹、上甩、抽屉、两级下拉、设置、按键、计时、计数保存、PIN 数学模型、天气解析与样本动画。Sony BLE 状态机另外使用本地 GATT 测试，覆盖连接、拍照、录像、超时、重连和退出。

实机待验项目包括启动与关机、Wi-Fi 配网、BLE 开关和相机、滑块持续调节、提示音与振动、计时结束、水平仪噪声/校准以及天气在联网、断网、DNS、TLS 和超时各阶段退出。天气反复进出及退出后立刻重开要一并检查。记录多轮运行后的内部 RAM、最大连续空闲块、PSRAM、撕裂、丢帧和触摸溢出。

持续交互的候选开销包括全屏 RGB565 软件合成、PSRAM 同步和打包、TE 相位、天气背景绘制及共享 I²C。量产配置与性能结论以实机记录为准。


## 构建与资源对比

| 指标 | V5 | V6 |
| --- | ---: | ---: |
| 应用镜像 | 4,771,008 B | 4,887,648 B |
| 静态 D/IRAM（链接器） | 153,307 B | 153,627 B |
| 静态 IRAM（链接器） | 16,384 B | 16,384 B |
| 链接后 MiSans 资源 | 1,771,000 B | 1,777,405 B |
| MiSans 20 / 26 动态字库 | 571,952 / 843,731 B | 571,952 / 843,731 B |
| 链接后抽屉图标 | 360,224 B | 441,120 B |
| 运行时 CPU、内部堆与 PSRAM | 待测 | 待测 |

两份固件使用 ESP-IDF 5.5.5。占用来自应用镜像和链接 map，V6 应用分区余量为 289,696 B（约 6%）。大部分镜像增量为 166×166 的原生 RGB565 抽屉图标，用于移除正常抽屉滚动时的图像变换。动态字库大小相同。LVGL 的两块 DIRECT 缓冲仍合计占 872,352 B 的像素数据。锁屏与抽屉缓存合计约 848 KiB 的 RGB565 像素，随 Launcher 释放，同时移除了 M5GFX 的额外面板帧缓冲。无线共存后的净堆余量仍需测量。

发布配置及启用性能统计的配置均交叉编译通过。采样构建启用 16 行 DMA、400kHz CST820 和 FreeRTOS 运行时间统计，作为实验产物。保留的告警包括 LVGL 选择器枚举组合，以及原有 HAL、上游组件的结构体初始化和弃用 API 告警。

桌面 DIRECT 和 FULL 回归均通过。同源抓帧 49 张逐像素比较一致，抽屉验证覆盖 8 项和 20 项、左循环、连续切换后索引重定位、实际横扫吸附及复用卡片点击。导出见 [系统总览](../ui-preview/screenshots/lvgl/overview.png) 和 [五应用总览](../ui-preview/five-apps/actual/overview.png)。

## 工程文件与清理范围

替换了 `main/apps/app_extensions/app_extensions.cpp` 和 `app_extensions.h`。应用注册与独立生命周期见 [系统架构](ARCHITECTURE.md)。主要修改还包括 `main/hal/hal_display.cpp`、`touch_history.h`、`display_frame.h`、`main/system/shell.cpp`、天气请求服务，以及固件、桌面和字体脚本。

基准 HEAD 包含 6,796 个文件，约 643.2 MiB。整改工作区包含约 6,820 个源文件和资源，约 644.2 MiB，计入本次新增的源码和截图，排除 Git 数据库及构建缓存。现有 Git 对象包为 358.40 MiB。

批量清理在工作区写入前被自动审查拒绝。两个完整上游参考副本、未使用的 MiSans 格式及字重副本、未注册官方应用和第三方示例/测试仍在。已核对固件及桌面入口均未注册七个官方旧应用。清理时需继续保留 HAL 仍引用的 `icon_badge.c`、图标及字体生成所需的原始文件、许可证和 UI 预览。全新配置关闭 LVGL 示例与 Demo 编译。原有 Git 历史保留。
