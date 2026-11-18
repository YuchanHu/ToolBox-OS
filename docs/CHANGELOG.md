# 开发记录

## 1.1

- 新增 Refresh V6。LVGL 仅在 DIRECT 缓冲区可复用时提交绘制，触摸及动画仍可在面板传输期间推进。
- 应用抽屉复用五个卡片槽位，锁屏过渡使用预生成快照。
- 五个工具应用采用独立生命周期，天气的网络收尾移出应用退出路径。
- 固件、桌面渲染和字体工具通过环境变量与项目相对路径定位依赖。

## 1.0

- 从 M5Stack 官方 StopWatch UserDemo 迁移至 ESP-IDF 工程，锁定 ESP-IDF 5.5.5 与 LVGL 9.5.0。
- 建立 Runtime、两级控制中心、数字与指针表盘、应用抽屉和系统设置。
- 接入 Wi-Fi 配网、BLE 开关、Sony 相机遥控、秒表与五个工具应用。
- Refresh V5 引入 CO5300 TE 同步、独立触摸采样与 RGB565_SWAPPED DIRECT 显示链路。

官方参考起点为 [M5StopWatch-UserDemo @ 81099c7](https://github.com/m5stack/M5StopWatch-UserDemo/tree/81099c784991dc77f522de1a5bf62ec5de9ae0b5)。
