# ToolBox OS 1.0

M5Stack StopWatch / ESP32-S3，ESP-IDF 5.5.5，LVGL 9.5。

## 当前阶段

按实机反馈重做核心 UI，阶段 3 系统功能已接入固件。启动进入数字锁屏，控制中心中的设置可选择指针表盘，选择保存到 NVS。锁屏上划进入保留循环与吸附交互的新版抽屉，普通 App 退出后返回所选锁屏。保留 StopWatch、Setup、IMU 用于硬件验证。

首次顶部下拉显示时间与真实电量，停留 1.5 秒后自动收起。期间再次下拉显示全屏控制中心，上划直接回到原页面。控制中心具备 Wi-Fi、亮度和音量操作，BLE 控件已接入协议栈及广播开关。秒表和设置已采用原型的页面布局与同款图标。IMU 诊断入口采用统一冷色和 MiSans。

UI 原型位于 [ui-preview/index.html](ui-preview/index.html)。首次下拉的小浮层现包含时间与电量。网络与电量使用样例数据。

## 架构

```text
app_main
  toolbox::Runtime
    HAL 初始化及长期服务
    FreeRTOS 系统命令队列
    Mooncake App 生命周期
      Launcher + 系统锁屏 / StopWatch / Setup / IMU
    LVGL 默认共享 MiSans
```

- `main/system/runtime.cpp` 集中初始化 HAL、Smooth UI 时钟、字体和 App，主循环执行硬件命令并驱动 Mooncake。
- `main/system/app_registry.cpp` 为唯一 App 注册入口。新 App 继承 Mooncake AppAbility，并在此注册。
- `main/system/runtime.h` 提供 Wi-Fi、BLE、配网、亮度、音量与表盘偏好保存命令。UI 回调使用非阻塞 `post`，返回 false 时保留待提交操作，在后续帧重试。
- 设置页已经接入命令队列。硬件操作及 NVS 保存由主任务在 LVGL 锁外执行。队列中的操作在页面退出后仍会执行。
- HAL 保有电池、RTC、网络、音频与显示状态。页面读取 HAL 提供的状态，避免重复建立服务状态副本。
- LVGL 对象操作沿用 `LvglLockGuard`。主任务的硬件命令执行完成后才进入 App 更新。
- `main/system/fonts.h` 统一提供 MiSans 28px 字体入口，已用于默认文本及启动标题，系统壳层和 App 文本统一使用 MiSans 20/28/36/64/96px 资源。
- 全局浮层挂载 `lv_layer_top()`，由 `main/system/shell.cpp` 持有。LVGL 输入回调将顶部保留区域手势交给 shell，其余触摸继续传给应用。浮层显示期间 Mooncake 生命周期持续运行。

## 构建

本机执行 `./tools/build.ps1`。脚本配置 ESP-IDF 5.5.5，并输出到 `build-toolbox`，便于区分原空项目的 build 缓存。

VS Code 的 `.vscode/settings.json` 已设置 `idf.buildPath` 为 `build-toolbox`，ESP-IDF 扩展的构建和烧录使用同一目录。修改设置后可执行 Developer Reload Window，再运行 ESP-IDF 的构建、烧录和监视操作。旧 `build` 目录中的固件属于早期迁移版本。

依赖首次获取使用 `python fetch_repos.py`，随后由 IDF Component Manager 按 `dependencies.lock` 获取托管组件。若需要本机代理，可在当前终端设置 HTTP_PROXY 与 HTTPS_PROXY。

字体转换使用 `npm ci --prefix tools/font-converter` 后执行 `./tools/generate-fonts.ps1`。转换器固定为 lv_font_conv 1.5.3，使用本地 MiSans Regular，4bpp，ASCII 与 main 下 C++ 源码中的中文字符子集。新增文案后重新生成字体。96px 仅包含时间数字与冒号。

设备端口为 COM3。本轮仅进行构建验证，烧录及实机验收由后续明确指令启动。

## 源码基线

官方仓库为 https://github.com/m5stack/M5StopWatch-UserDemo ，提交 `81099c784991dc77f522de1a5bf62ec5de9ae0b5`。

已获取官方浅克隆，并一次性对照用户提供的本地示例，忽略 CRLF 差异后本地快照文件内容一致。对照目录为 `reference/upstream-baseline`。工程根目录原本没有 Git 仓库。

依赖版本沿用官方 repos.json，M5IOE1 与 M5PM1 官方补丁已应用。ArduinoJson 是 JSON 库，工程使用 ESP-IDF，未引入 Arduino Framework。

首次构建发现 M5GFX 旧版 register_component 对带空格的工程路径处理失败，已改用 idf_component_register，并保存至 patches/M5GFX.patch，由 repos.json 在获取依赖时应用。

原空项目 sdkconfig 保存在 `sdkconfig.initial`，当前 sdkconfig 基于官方 sdkconfig.defaults 生成。显示传输及缓冲机制沿用官方代码，当前未进行刷新性能优化。

## 待后续阶段验证

- 实机确认锁屏、跟手上划、全局浮层的显示和交互效果。
- 实机验证 BLE 扫描可见性、开关切换、重新配网及手机网页联网流程。
- 实机启动、触摸、电源、联网、音量及显示撕裂表现。
- MiSans 发行许可文件补齐，后续新增文案的字体字符集维护。

## 本轮验证结果

2026-11-16 使用 ESP-IDF 5.5.5 完整构建成功，最终版本修改的增量构建也通过。产物为 build-toolbox/ToolBoxOS.bin 与 ToolBoxOS.elf。阶段 1 应用大小 0x305990 字节，最小应用分区剩余 39%。阶段 2 最新固件大小为 0x279790 字节，最小应用分区剩余 50%。官方 HAL 与部分测试 App 仍有初始化成员和枚举转换等编译警告。设备端口 COM3，实机验证待进行。

原型通过 Edge 字体及交互检查，首次下拉电量截图为 ui-preview/screenshots/clock.png。


## 阶段 2 实现与验证

`main/system/shell.cpp` 实现数字/指针锁屏、系统浮层和全局触摸路由。指针刻度采用与原型一致的长短线段，数字、日期、电量圆环和指针尺寸按原型重建。所有对象操作在 LVGL 线程或持有 LvglLockGuard 时执行。硬件命令与表盘偏好保存经 Runtime 队列发送到主线程。

锁屏拖动距离直接驱动钟表上移淡出、抽屉上移显现。220px 为完整进度，松手 99px 及以上进入抽屉，否则回弹，松手补间为 240ms。该阈值沿用原型设计。锁屏不接收左右切换，抽屉和 App 上划不会进入抽屉。秒表、设置和抽屉提供屏幕返回按钮，硬件 A+B 长按返回保留。控制中心设置入口打开完整设置菜单。

数字锁屏每秒检查时间，仅变化的标签触发 LVGL 重绘。指针表盘目前按秒更新秒针，电量和日期读取 HAL/RTC 系统时间。硬件命令队列满时，控制中心保留未提交值并在后续 timer 中重试。

本阶段完成交叉编译和分区大小检查。COM3 设备的启动、触摸、刷新帧率及视觉效果仍待烧录后实测。

## 阶段 3 系统功能

控制中心的 BLE 开关使用 ESP-IDF Bluedroid BLE 接口。开启后广播名称为 ToolBox OS，广播类型为非连接广播。关闭会停用 Bluedroid 和蓝牙控制器，初始化资源保留以供再次开启。BLE 默认关闭，当前功能为射频与设备广播控制，后续 App 可扩展业务服务。设置列表 Bluetooth 行也可切换，下一行显示状态与错误。

Wi-Fi 设置显示真实 SSID、IP 和连接状态，并提供开关、重新配网、退出配网。首次开启且没有保存网络时自动进入配网，设备页面显示实际 AP 名称和 URL。退出配网时有保存网络则尝试连接，没有保存网络则关闭 Wi-Fi。网络凭据通过官方 SsidManager 保存到 NVS，已保存网络的自动重连继续由官方 WifiStation 驱动。

手机页面使用系统字体，主源文件为 main/assets/provision.html。CMake 将其同步到配网组件的嵌入资源。页面使用 /scan、/submit 与 /exit 接口，连接成功并保存后自动请求退出配网。浏览器模拟接口验证已覆盖带引号 SSID、JSON 凭据、连接失败后的重试和成功退出，脚本错误为零。该检查使用模拟响应，设备的射频联网仍待实测。

官方 esp-wifi-connect 3.1.3 复制到 components/esp-wifi-connect，main/idf_component.yml 使用 override_path 指向此源码。组件的网络管理、NVS、扫描、连接验证与退出实现沿用官方代码。本轮对提交处理补齐分段请求体读取，扫描结果使用 cJSON 转义 SSID，并替换手机页面资源。服务端的已保存网络及高级设置接口继续保留。

亮度和音量的拖动变化立即经系统队列应用，松手后保存。控制中心会从 HAL 同步其他设置页面做出的调整。

实机验收使用 COM3 设备，检查首次配网、错误密码重试、成功联网、重新配网、退出配网、Wi-Fi 关闭再开启，以及 BLE 广播开关。控制中心覆盖秒表时应保持原计时状态。正式显示性能测量属于下一阶段。

阶段 3 编译使用 ESP-IDF 5.5.5，BLE 传统广播接口通过 CONFIG_BT_BLE_42_FEATURES_SUPPORTED 启用。完整交叉编译与分区大小检查通过。当前固件约 2.90 MiB，应用分区剩余约 41%。手机页的失败重试和成功后延迟扫描检查均通过。设备端无线功能与触摸显示尚待 COM3 实机验证。

## 实机反馈后的 UI 重做与下拉规则

核心 UI 已按已确认原型的 466×466 圆形画布重建，图标直接由 ui-preview/app.js 中的 SVG 路径导出。数字表盘使用 MiSans Semibold 98px，指针表盘使用 36px 小时数字、长短刻度、白色时分针、冰蓝秒针与电量圆环。抽屉采用 150px 中央圆角图标、两侧导航、应用名和圆形分页点，保留循环滚动与居中吸附。

控制中心使用 Wi-Fi/BLE 图标卡片、亮度/音量滑块卡片、电量和设置入口。设置、网络状态、AP 配网与秒表按原型的间距、字号和圆角重建。秒表复用官方计时模型，并提供屏幕返回、复位、开始和暂停。

第一级浮层停留 1500ms 后以 200ms 动画收起。第二次下拉进入全屏控制中心，从控制中心上划直接关闭全部浮层。超时与手势采用 LVGL tick 计时，浮层继续独立于 App 生命周期。

桌面渲染工具位于 tools/ui-render，使用固件中的 ui.cpp、shell.cpp、stopwatch_ui.cpp、抽屉源码、实际字体与图标，以及相同 LVGL 9.5 软件渲染器。硬件读数采用固定样例。执行 tools/ui-render/build.cmd、build-ui-render/render.exe 和 python tools/ui-render/export.py 可生成 ui-preview/screenshots/lvgl/overview.png 与逐页截图。工具已验证锁屏回弹/进入、1500ms 超时、第二级直接退出和退出后保持隐藏。浏览器原型也已通过对应交互检查。

当前验证覆盖软件渲染和交叉编译。COM3 设备的真实触摸流畅度、面板效果与图层动画需烧录复测。

本轮最终构建通过，ToolBoxOS.bin 为 3039376 字节，应用分区剩余 41%。已导出并检查 11 张 466×466 的固件同源 LVGL 页面图，包含启动页。VS Code 烧录目录已核对为 build-toolbox。

## 惯性、对齐与秒表按键修正

锁屏上划加入松手速度投影。慢速短拖回弹，短距离快速上甩继续进入抽屉，使用最近采样的纵向速度和 160ms 投影判断，并保留 24px 最小有效拖动距离。数字状态行、两级下拉的图标与文字采用内容宽度自动居中的布局，文本按 MiSans 数字的可见高度修正垂直位置。菜单内容统一从 y=144 开始，为结束于 y=128 的返回按钮留出间距。

全部电池形状控件使用真实电量驱动内部实心填充。测试覆盖 0%、25%、50%、82%、100%，并验证 7% 与 100% 时数字表盘状态行仍然居中。

秒表恢复官方按键触发方式。B 键按下时开始、暂停或继续，A 键在运行时计圈、暂停时复位，停止状态的 A 键保持无操作。触屏左侧按钮采用相同行为，并新增记录入口显示累计计圈时间，最近记录在前。A+B 长按返回。按键状态每轮只采样一次，复用官方 Button_Class 和 KeyManager。

桌面检查已经编译并执行实际 AppStopWatch、官方按键去抖、KeyManager、计时模型和新版 LVGL 页面，覆盖启停、继续、记录、复位及组合键返回。惯性上甩、低速回弹、菜单避让、实心电量以及下拉浮层超时/直接退出检查通过。截图增加 stopwatch-records.png。设备上甩灵敏度及物理按键体验仍需实机复测。
