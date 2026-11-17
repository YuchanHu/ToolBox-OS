# ToolBox OS 1.0 视觉规范

## 画布与资源

圆形可视区域为 466×466。依据本地示例 `main/hal/hal_display.cpp`，驱动面板配置为 468×466、横向偏移 6，控制器内存为 480×480。`app_watch_face/view/simple.cpp` 使用 466 遮罩。原型按该遮罩设计，核心内容在中心 328px 宽区域排列。

全部文本使用本地 `reference/MiSans/woff2` 的 Regular 与 Semibold。原始字体文件保持原样，原型 assets 中保存这两个字重。当前资源目录未发现许可文本，发布字体资源前需补齐官方许可记录。

## 色彩

|用途|色值|
|---|---|
|屏幕背景|#000000|
|卡片|#181D25|
|控件|#202631|
|主文字|#F3F6FC|
|次级文字|#98A4B7|
|主操作|#9DC6FF|
|选中操作文字|#081627|

## 尺度

- 表盘时间 98px / Semibold，日期 25px / Regular。
- 页面标题 26px / Semibold，主要行文本 23px，网络详情 19px。
- 秒表数字 65px，百分秒 28px，使用等宽数字特性。
- 卡片圆角 22–28px，抽屉主图标圆角 46px。
- 图标统一使用 24 单位 SVG 路径，常规显示为 28px，线宽 1.8。
- 核心触摸目标至少 46px，常规间距 8、14、16、24px。
- 动画目标 240ms，曲线 cubic-bezier(.22,1,.36,1)。

## 源码复用结论

- Launcher 的 `LV_SCROLL_SNAP_CENTER` 和五组重复图标可复用循环吸附逻辑。
- WatchFace 的 RTC 时间与圆形遮罩可复用，表盘主题切换交互按新需求重做。
- ArcTopClock 已按字符旋转排列数字，采用 HAL 时间与更新节流。新设计使用顶部数字标签与 MiSans 数字。
- StatusBar 已有 Wi-Fi、电池读取与 spring 动画。全局浮层需在后续固件阶段放到系统生命周期中。
- Settings 的 NVS Get/Set 接口与 HAL 亮度保存可复用。原有字体资产含 Montserrat、Commissioner、Maple Mono，迁移时统一替换为 MiSans。
- HAL Display 已启用 M5GFX framebuffer、RGB565、PSRAM 双 partial buffer、QSPI 80MHz。TE 引脚为 GPIO38，当前 flush 中未见等待 TE 的逻辑。性能与撕裂效果留待固件编译和实机验证。
- `hal_network.cpp` 提供 WifiManager/SsidManager 与配网状态。原型沿用 M5StopWatch 热点前缀与 192.168.4.1 地址。本地 `config_ap` 中的 badge 编辑网页与 Wi-Fi 凭据配网页用途不同，正式阶段需沿网络管理器接入凭据提交接口。

本轮仅创建 ui-preview 文件。字体、图标、页面均由本地字体和 HTML/CSS/SVG 直接渲染。

## 指针表盘

新增 Apple Watch 简洁指针表盘风格的圆屏设计，包含 60 格刻度、MiSans 小时数字、白色时分针、冰蓝秒针、日期及电量圆环。画布为 466×466。指针预览时间选用 10:09:30，以便查看指针形状与层级。

