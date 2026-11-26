# Sony 相机高级控制开发进度

阶段 4 的拍照、录像和参数控制已实现。用户用已刷入的阶段 4 固件通过手机辅助配网连接相机，反馈基本功能正常，并提出六项体验修复。修复版已完成构建和桌面验证。板载重连、逐项参数读回、录音静音及实际滑动帧数仍待实机验收。

阶段 5 的机身按键、融合控制转盘、前后拨轮和自定义键已实现，构建、协议模拟和桌面 LVGL 回归通过。机身按键与拨轮的真实 ID、动作和方向仍待 A7M5 实机验收。

阶段 8 完成性能、并发、资源释放和 UI 统一整改，构建及全部桌面回归通过，固件已刷入 StopWatch，等待用户按 [阶段 8 验收报告](SONY_CAMERA_STAGE8.md#6-a7m5-实机验收清单建议顺序) 手动验收。

以 [任务书](SONY_CAMERA_TASK.md) 为规范，以 [V4 HTML](../ui-preview/sony-camera/sony_camera_ui_v4.html) 为视觉交互基准。

| 阶段 | 推荐模型 | 推理强度 | 状态 |
|---|---|---|---|
| 1 项目检查及协议验证 | GPT-6.2 Sol | High | 离线交付完成，基础协议已由阶段 2 在 A7M5 验证 |
| 2 ESP32-S3 最小通信 | GPT-6.2 Astra（额度紧张先 Sol） | High | 完成，A7M5 基础通信、属性读取、拍照、录像启停及退出通过实机验证 |
| 3 连接配置及 UI | GPT-6.2 Sol | Medium | 实现、构建及桌面回归通过，板载验收待执行 |
| 4 拍照、录像及参数 | GPT-6.2 Sol | High | 用户已验证手机辅助连接及基本功能，修复版构建和桌面回归通过，专项实机待验收 |
| 5 机身控制 | GPT-6.2 Sol | High | 实现、构建及桌面回归通过，A7M5 按键与拨轮待实机验收 |
| 6 媒体管理 | GPT-6.2 Sol | High | 实现、固件构建及桌面模拟通过，A7M5 媒体与删除范围待实机验收 |
| 7 自动化摄影 | GPT-6.2 Sol | Medium | 间隔拍摄与预设已实现，固件构建及桌面测试通过，实机待验收，焦点堆栈入口隐藏 |
| 8 性能优化及整体验收 | GPT-6.2 Astra（额度紧张先 Sol） | High | 整改、构建及桌面回归通过，固件已刷入，整体实机验收待用户执行 |

模型名称及可用推理等级以实际 Codex 选择器为准；Extra High 仅用于难解阻塞。

阶段执行统一命令：阅读 docs/SONY_CAMERA_TASK.md、docs/SONY_CAMERA_PROGRESS.md 和 ui-preview/sony-camera/sony_camera_ui_v4.html。本轮仅执行任务书第 N 阶段，检查上一阶段验收证据与阻塞，遵守全部约束，完成相关实现、测试和构建（适用时），更新进度文件，列出实际结果和待实机验收项，完成后停止，不提前执行下一阶段。不擅自推送或刷机。

## 交接记录
每轮追加：阶段、完成项、变更文件、参考版本、证据（区分模拟/实机）、构建命令和结果、资源占用、阻塞、待验收及下一阶段建议。

### 2026-11-25 · 阶段 1 项目检查及协议验证

前置证据为 `b9a17ebe5142f8fba63cb95ce194c87f3a77edd8` 归档的任务书、进度初稿和 V4 HTML。阶段 1 是首个开发阶段。既有 ToolBox OS 1.2.0 的 BLE、配网、反馈、显示和桌面回归作为基线；V4 交互使用模拟数据。已读取 `?overview` 入口及按键、手势和录制逻辑，以任务书为行为和数据约束。

**本轮变更**

- [SONY_CAMERA_STAGE1.md](SONY_CAMERA_STAGE1.md) 记录系统架构及复用点、参考源码与许可证、A7M5 协议矩阵、Windows 探针步骤和实机验收清单。锁定 alpha2go `02883425`、FX30-Controller `85604cbe`、Alpha-Fairy `fd1f2989`、libssh2_esp32 `26814617`、LibSSH-ESP32 `343fce1c`、esp_new_jpeg `7162d323`，完整提交和源码位置见该文档。
- 本进度文件记录证据、资源基线和阶段衔接。固件、原型和依赖没有代码改动，参考文件和探针产物仅用于本机验证。

**构建与桌面测试**

| 验证 | 实际结果 |
|---|---|
| ESP-IDF 5.5.5 固件 | `./tools/build.ps1` 增量构建通过，`./tools/build.ps1 -BuildDirectory build-sony-stage1` 全量构建通过。使用已安装工具链，初次受沙箱内 `WinError 623` 影响，随后通过工具执行授权完成。全量构建仍有现有 `hal_audio.cpp`、`shell.cpp` 枚举转换警告。本地基线产物 `build-sony-stage1/ToolBoxOS.bin`，对应 ELF 和 MAP 在同目录。 |
| ToolBox OS 现有桌面回归 | `tools/ui-render/build.cmd` 配置和构建通过。`build-ui-render/camera_backend_check.exe` 通过，BLE 模拟回调覆盖认证、订阅、快门序列、先到/后到录像通知、忙碌拒绝、超时、重连和关闭。`render.exe` 及 `render.exe full` 通过，覆盖现有页面、KEYA/KEYB、手势和显示路径，使用桌面桩。 |
| alpha2go 桌面测试及探针 | Windows Go 1.26.0 便携环境中 `go test -count=1 -v ./...` 通过，覆盖本地模拟 SSH/PTP-IP、Sony 控制与媒体，以及上游 α6700 属性/信息样本的解析。`go build -o ptpprobe.exe ./cmd/ptpprobe` 和 `ptpprobe.exe -h` 通过。Go 默认代理超时，以公开镜像离线缓存同版本模块并保持 `go.sum`，显式使用官方 Go 包中的 `zoneinfo.zip` 解决桌面时区用例。未改变上游源码。 |
| FX30-Controller 桌面测试 | 用现有 MSVC 编译 `test_framing.cpp` 和 `test_ptp_parsers.cpp`，运行 9 个测试、67 项断言，零失败。集成及属性测试依赖 POSIX socket，Windows 未运行这两组。上游真实 FX30 样本和本地解析测试均只作为参考证据。 |
| 本项目 A7M5 及 StopWatch 实测 | 本轮没有可用 A7M5 会话或板载测量，实际相机关联、身份验证、拍摄、录像、媒体和硬件回归均未验收。未推送、刷机或删除相机媒体。 |

离线复核两份上游 `0x9209` 抓样。α6700 样本的 352 项完整解析，FX30 v300 样本以其上游解析器解出 351 项。两份样本的 `0xD208/D20A/D20C` 都是 `u16`，`0xD21D` 是只读 `u8`，已在能力矩阵标注与笔记的差异及证据边界。

**Flash 与静态 RAM 基线**

- 16 MiB Flash。按 `partitions.csv` 和构建生成表，`factory` 从 `0x20000` 起，大小 `0xBD0000`（12,386,304 B）。`nvs` 16 KiB、`phy_init` 4 KiB、`storage` 4 MiB、`coredump` 64 KiB。构建前和全量构建后，APP BIN 均为 `0x4AE720`（4,908,832 B，体积差 0 B），剩 `0x7218E0`（7,477,472 B，约 60.4%）。
- `idf.py -B build-toolbox size-components` 和新 MAP 的 `python -m esp_idf_size` 实测一致。Flash 段 4,777,144 B；静态 IRAM 16,384 B（`.text` 15,356 B / 向量 1,028 B）；共享 D/IRAM 静态占用 149,427 B（`.data` 26,940 B / `.bss` 34,264 B / `.text` 88,223 B）。静态表中的 192,333 B 剩余是链接区域口径，运行时内部 Heap 待测。

| 链接组件 | Flash `.text` + `.rodata`（B） | 静态 RAM（B） |
|---|---:|---:|
| `main`（含字库、图标） | 2,532,189 | 8,531 |
| 其中既有相机三对象（`app_camera`、`camera_ui`、`camera_remote`） | 16,836 | 159 |
| `lvgl` / `M5GFX` | 361,134 / 72,427 | 636 / 536 |
| `bt` / `btdm_app` | 259,342 / 65,050 | 1,338 / 17,631 |
| `net80211` / `lwip` / `wpa_supplicant` / `esp-wifi-connect` | 140,367 / 98,469 / 64,714 / 36,686 | 8,555 / 3,871 / 1,371 / 120 |
| `mbedtls` + `mbedcrypto` + `mbedx509` | 190,747 | 680 |

显示缓冲按源代码配置计算，`468×466×2` 每帧 436,176 B，两帧请求 PSRAM 共 872,352 B；两块 8 行 DMA 条带请求内部 RAM 共 14,976 B。CST820 采样与 DMA 任务栈各配置 4,096 B，LVGL 栈 16,384 B。这些值是分配请求或配置，不含分配器开销。运行时内部 Heap、最大连续块、PSRAM 余量、任务栈高水位以及新增 SSH/JPEG 开销均待测。现有 M5GFX JPEG 对象的已链接占用为 0，启用后需重测。

**阻塞与下一阶段衔接**

- 优先按[实机清单](SONY_CAMERA_STAGE1.md#a7m5-实机验收清单)补 Windows 2.4 GHz 直连、指纹、认证算法及 SSH 双转发、PTP/IP/SDIO、型号与属性。继续记录参数读回、拍照、真实录像状态/时长、物理按键同步、机身按钮与拨轮、媒体与时间。删除等待指定无价值测试内容的用户确认。
- 阶段 2 接入时以 `hal_network.cpp` 的网络管理和现有 IDF mbedTLS 为基础。唯一 SSH 客户端候选为 `playmiel/libssh2_esp32` 固定提交，尚需独立确认 IDF 5.5.5 链接、实机算法交集和峰值内存。复用已证实的桌面握手、样本和工具，先完成最小链路、型号/属性、拍照、录像与关闭/重连。采样前后 BIN、剩余分区、链接组件、内部 Heap/PSRAM 和栈，基础链路实机验收后才将其标为可用。

### 2026-11-25 · 阶段 2 ESP32-S3 最小通信

**实现与变更文件**

- `components/esp-wifi-connect` 中的 `wifi_manager`、`wifi_station` 复用既有驱动、STA 接口和 DHCP 事件。诊断任务独占空闲 Wi-Fi，使用本轮输入的 WPA2 或兼容热点凭据。直接关联失败结束诊断，凭据留在内存。退出时关闭 STA 并回到正常启动及原有系统网络策略。
- `main/sony/tunnel*` 用相机 DHCP 网关的 IPv4 地址连接 SSH 22 端口。操作员核对机身完整 SHA-256 指纹后才可提交 SSH 用户名和密码。支持单个隐藏的 keyboard-interactive 密码提示，服务器仅公布 password 时使用 password。复用同一个 SSH 会话打开两路 `direct-tcpip` 到字面量 `localhost:15740`。
- `main/sony/session.*` 完成 PTP/IP 命令和事件通道握手、事务编号及数据长度校验、`GetDeviceInfo` 型号、PTP 会话和 Sony SDIO 三阶段握手。当前启用 v300 描述符，`0x9209(0)` 提供类型、当前值、可写性和原始允许范围或枚举。快门按会话能力发送 `D2C1/D2C2` 半按/全按及释放。录像使用 `D2C8` MOVIE 按下及释放。发命令前重新读取 `D21D` 并比对操作员在机身观察后登记的原始状态。相机应答后输出属性读回供核对，机身 REC 和测试卡用于确认最终结果。断线或帧错误使录像状态变为未知。
- `main/main.cpp`、`main/CMakeLists.txt`、`tools/build.ps1` 接入可选 USB Serial/JTAG 诊断。构建时选择 `-SonyCameraProbe`，启动时由操作员输入 `P` 才启用。FreeRTOS 前台任务串行持有会话，诊断退出后重启回到正常运行。默认构建关闭诊断入口。
- `components/camera_ssh` 保留 [libssh2_esp32 26814617](https://github.com/playmiel/libssh2_esp32/tree/26814617eb1f880c6f998e2744f15918947ec577) 所需 BSD-3-Clause 源码和许可证，包含上游版权文件。目录内的 CMake 接入 IDF 自带的 mbedTLS，`libssh2_setup.h` 增加桌面回环编译条件。Sony 协议实现参照 [alpha2go 02883425](https://github.com/Knallli/alpha2go/tree/02883425c9445c653b2950210187cdd1547320c2)，Apache-2.0 许可和归属随 `main/sony/licenses` 交付。
- `tools/sony-test`、`tools/ui-render/CMakeLists.txt` 增加同源的协议模拟和本机回环测试。Go 用于运行固定版本的桌面模拟相机。原型和正式相机 UI 沿用本轮开始时的版本。

**测试与构建证据**

| 检查 | 实际结果 |
|---|---|
| ESP-IDF 5.5.5 诊断镜像 | `./tools/build.ps1 -BuildDirectory build-sony-stage2 -SonyCameraProbe` 编译、链接及分区校验通过，产物为 `build-sony-stage2/ToolBoxOS.bin`，同目录保留 ELF、MAP 和烧录参数。 |
| ESP-IDF 5.5.5 默认镜像 | `./tools/build.ps1 -BuildDirectory build-toolbox` 编译、链接及分区校验通过，产物为 `build-toolbox/ToolBoxOS.bin`。链接报告中没有 SSH 组件段。保留现有音频和 LVGL 枚举警告。 |
| Windows PTP/IP 协议模拟 | `build-ui-render/sony_protocol_check.exe` 通过。覆盖命令/事件连接号、探活、SDIO 协商重试、分段数据、属性读回、状态变化时拒绝动作、拍照顺序、录像两次独立操作、相机繁忙及释放失败、InitFail、超长数据、EOF 和事务号错误。 |
| Windows SSH 回环模拟 | 使用与固件同一固定版本的 libssh2 源码及 IDF 附带的 mbedTLS 源码，采用 Windows 的 socket 和 mbedTLS 平台配置。错指纹时服务端观察到 0 次凭据尝试，错密码收到 1 次尝试并拒绝。正确凭据的两次独立会话均经 keyboard-interactive 完成认证，打开两路转发，读取型号、属性并收到独立事件。模拟协商为 ECDH P-256、ECDSA P-256、ChaCha20-Poly1305，SSH 方法报告的 MAC 为 HMAC-SHA2-256。 |
| 上游抓样离线回放 | α6700 的 `a6700-9209.bin` 完整读出 352 项，`D21D` 为只读 `u8`，样本值 0。FX30 v300 抓样包含上游解析器也需重新同步才能跳过的异常条目。本实现严格校验完整数据集，因此拒绝该抓样。两份样本均来自上游其他机型。 |
| 既有 BLE 桌面回归 | `build-ui-render/camera_backend_check.exe` 通过，覆盖认证、订阅、快门、录像通知、繁忙、超时、重连和关闭。 |
| 既有 LVGL 桌面回归 | `build-ui-render/render.exe` 和 `render.exe full` 均通过，使用现有桌面桩。 |
| A7M5 / StopWatch 实机 | 开发板通过 Espressif USB Serial/JTAG（COM3）经授权烧录和操作。相机实机证据、板载内存和现场限制见下方实测记录。 |

**Flash 与静态 RAM**

工厂 APP 分区为 12,386,304 B。阶段 1 BIN 为 4,908,832 B，阶段 2 默认 BIN 为 4,908,576 B，剩余 7,477,728 B。修正串口空闲退出后的阶段 2 诊断 BIN 为 5,004,560 B，较阶段 1 增加 95,728 B，剩余 7,381,744 B（59.6%）。

使用 `python -m esp_idf_size` 读取 MAP。默认镜像 Flash 段 4,776,888 B，静态 IRAM 16,384 B，共享 D/IRAM 静态占用 149,427 B。诊断镜像 Flash 段 4,869,540 B，静态 IRAM 16,384 B，共享 D/IRAM 静态占用 153,255 B（增加 3,828 B）。其中 `.data` 27,116 B，`.bss` 34,768 B，共享区 `.text` 91,371 B。

| 诊断镜像中的归属 | Flash `.text` + `.rodata`（B） | 静态 RAM（B） |
|---|---:|---:|
| libssh2 组件 | 64,378 | 692 |
| Sony 会话、SSH 传输和诊断 4 个对象 | 10,531 | 0 |
| 既有 Wi-Fi 组件（包含新增 STA 路径） | 37,114 | 120 |
| 既有 mbedTLS、mbedcrypto、mbedx509 | 191,365 | 680 |

诊断代码在 PSRAM 请求 65,536 B 的 PTP 工作缓冲，任务栈配置为 16,384 B，USB 收发缓冲请求 1,536 B。代码已在进入诊断、STA、SSH 密钥交换后、SDIO 就绪及关闭后采样内部 Heap、最大连续块、PSRAM、最大连续 DMA 和栈高水位。实际运行占用和 SSH 握手峰值待在板上采集，配置和分配请求仅作容量参考。

**复测、实机门槛与交接**

1. 桌面复测时，在已导出 ESP-IDF 5.5.5 和 MSVC 构建环境的 PowerShell 中配置 `cmake -S tools/ui-render -B build-ui-render -G Ninja -DSONY_SSH_LOOPBACK_TEST=ON`，再运行 `tools/ui-render/build.cmd`。单独运行 `sony_protocol_check.exe`，可传入固定 alpha2go 版本的 `ptpip/testdata/a6700-9209.bin`。进入同一 alpha2go 模块目录，以 Go 1.26 执行 `go run -mod=readonly <工作区>/tools/sony-test/loopback.go <工作区>/build-ui-render/sony_ssh_check.exe`，替换为实际绝对路径。模拟服务只监听本机回环。
2. 先复用阶段 1 的 Windows 探针完成 A7M5 只读验证，并保持机身访问身份验证开启。随后在授权的 StopWatch 环境启动阶段 2 镜像。串口终端关闭本地回显和日志落盘，上电后 4 秒内输入大写 `P`。输入机身 Wi-Fi Direct 信息，核对完整指纹后输入 `MATCH`，再输入单独的访问身份验证凭据。核对关联、DHCP 网关、算法、双转发、型号、SDIO 版本、能力与事件。缺 DHCP 网关或算法不交集时记录实际原因。
3. 先以 `props` 和 `state` 采集真实属性。相机静止时输入 `mark-idle`，通过机身物理按键开始录像并观察 REC 后输入 `mark-rec`，对照两次 `D21D` 是否不同，再通过机身结束录像。使用测试卡执行 `photo`、`start`、`stop`，检查写卡和 REC，核对忙碌、机身改参、掉线及重连。每次新会话重新观察和登记。本阶段以检查 MOVIE 按键序列和原始属性读回为限，A7M5 的录像枚举及真实时长仍需实机证据。
4. 使用 `quit` 或 Ctrl+C 结束诊断，检查会话资源和 STA 释放。分别验证正常启动及退出后的家庭 Wi-Fi、配网和原 BLE。保存已脱敏的内存采样与协议日志，并记录相机固件和模式。取得 A7M5 基础通信证据后，阶段 3 可复用 `Tunnel`、`Session` 和 RAM-only STA 入口，把网络所有权交还既有 HAL，并按任务书接入配置、指纹持久化及原网络策略恢复。

#### 2026-11-25 · A7M5 与 StopWatch 实机验收

- 经授权通过 USB Serial/JTAG（COM3）把 5,004,576 B 的诊断镜像写入 factory 应用分区 `0x20000`，esptool 校验通过。此次烧录范围为应用镜像。
- StopWatch 按机身提供的 Wi‑Fi Direct 信息关联成功。实测为 2.4 GHz、信道 1、20 MHz、WPA2-PSK、RSSI −48 dBm，DHCP 获得 IPv4 和网关。SSH 22 端口连通，协商得到 `ecdh-sha2-nistp256`、`ecdsa-sha2-nistp256`、`aes128-ctr`、`hmac-sha2-256` 并显示主机 SHA-256 指纹。首轮在人工指纹核对点退出，串口确认 STA 停止并恢复启动。相机固件、SSH 用户认证、双转发、PTP/IP、SDIO、属性及拍摄待继续采样。

| 板载采样点 | 内部 Heap 当前/最小/最大连续（B） | PSRAM 当前/最小（B） | 最大连续 DMA（B） | 诊断任务栈高水位余量（B） |
|---|---:|---:|---:|---:|
| 进入诊断 | 262,415 / 230,384 / 180,224 | 8,386,156 / 8,386,156 | 180,224 | 15,008 |
| STA、DHCP 就绪 | 192,583 / 160,264 / 118,784 | 8,320,244 / 8,318,024 | 118,784 | 13,360 |
| SSH 密钥交换后 | 191,139 / 158,500 / 114,688 | 8,237,220 / 8,231,924 | 114,688 | 13,152 |
| 关闭 SSH、停止 STA 后 | 193,459 / 158,500 / 118,784 | 8,322,496 / 8,231,924 | 118,784 | 13,152 |

Heap 最小值按 ESP-IDF 本次启动以来的统计口径读取。采样使用诊断启动路径，正式 UI 并发时的余量需另测。

第二轮直连出现过两次 STA 关联失败，均发生在 SSH 连接之前。后续重试成功。操作员核对机身 SHA-256 指纹并提供访问身份验证凭据，开发板通过 SSH 用户认证、双路 `localhost:15740` 转发、PTP/IP 初始化及 Sony SDIO v300。实机 `GetDeviceInfo` 读得 `ILCE-7M5`，扩展清单公布 484 个属性、60 个控制，含快门和 MOVIE。只读 `props` 输出 414 个标量描述符，包含曝光、白平衡、对焦、ISO、录像格式和电量。待机时 `D21D` 是只读 `u8`，值为 0。已收到事件通道的 `0xC203` 通知。

SDIO 就绪时内部 Heap 当前/最小/最大连续为 188,955 / 156,268 / 114,688 B，PSRAM 当前/最小为 8,171,684 / 8,155,068 B，最大连续 DMA 为 114,688 B，任务栈高水位余量为 13,152 B。

经操作员确认测试卡与取景已准备，发送了一次快门按下/释放序列。相机应答成功，事件通道收到 `0xC206`、`0xC201(0xFFFFC001)` 和 `0xC234`。机身随后提示正在写入存储卡，并暂时拒绝机身 MOVIE。此时实机 `D222=0x0011`，`D215=0x8001`，与 α6700 的卡和目的地同时保存、存在一个待传输对象的协议参考一致。退出诊断后，操作员在机身确认卡中已写入测试照片，并自行将保存目的地设为「仅拍摄装置」。阶段 2 会话没有写入相机参数或下载照片原件。

- **重连与串口生命周期。** 相机离开「Wi‑Fi Direct信息」页后，曾出现 STA 提前失败。回到该页后原镜像成功重连，确认了本次测试的机身准备条件。诊断会话曾在串口仍打开时提前退出。诊断入口将 USB 瞬时连接状态从退出条件移除，沿用输入超时、`quit`、Ctrl+C 和事件通道错误的退出路径。用 ESP-IDF 5.5.5 重新构建并经授权写入 5,004,560 B 应用镜像，校验通过。新版实际完成了重连、录像和退出的完整验证。
- **录像。** 操作员按机身 MOVIE，确认出现红色 REC，开发板读取 `D21D=1`。通过开发板的 MOVIE 操作停止后，500 ms 读回仍为 1，后续读取为 0。在同一会话由开发板单独完成一次短片启停，`D21D` 的真实读回为 `0 → 1 → 0`，事件通道收到 `0xC234`。操作员确认机身 REC 熄灭且在回放里看到了测试短片。
- **关闭与资源。** 最终 `quit` 后 STA 停止，诊断退出并恢复正常启动。该次会话结束时内部 Heap 当前/最小/最大连续为 193,459 / 156,256 / 118,784 B，最大连续 DMA 为 118,784 B，诊断任务栈高水位余量为 13,040 B。两次烧录均使用同一 factory 应用分区，保留现有设置和用户存储。代码未推送。
- **下一阶段使用的实测条件。** 以已验证的 Wi-Fi Direct、SSH 和双通道会话为基础。机身需按实测进入直连信息页。录像状态由事件提示后读回确认，实测的首次读回存在延迟，后续应在有限时间内等待机身状态变化。保留「保存到目的地」模式下待传输对象可能暂时阻塞录像的记录，后续仅使用机身公开的允许值，经用户确认后调整。相机固件版本及拍摄档位未记录，其它档位、持续录制时长、录制中失联、正式 UI 并发资源及全系统板载回归留待相应阶段。

### 2026-11-25 · 阶段 3 连接配置及 UI

**实现与变更文件**

- `main/apps/app_camera/app_camera.*`、`main/sony/connection_ui.*` 接入 V4 的 466×466 连接方式、配网方式、手机辅助、选择相机、键盘、连接中、指纹、控制台和反馈设置。选择蓝牙即进入原有 `CameraPage` 和 BLE 遥控流程。高级控制台显示相机读回的型号和连接状态，按任务书隐藏后续功能入口。手机页用实际临时热点密码和网页地址引导手机操作。
- `main/sony/keypad.*`、`credentials.cpp` 支持四种键盘、850 ms 多击、多击切换、独立空格、退格、符号翻页及 KEYA/KEYB。手机和本机共用凭据校验，支持完整可打印 ASCII、最长 32 字节 SSID、WPA2 密码及相机 SSH 输入。键盘始终明文，长值在固定边界内显示尾部。标题沿用 MiSans，`main/assets/fonts/toolbox_misans_sony_alpha.c` 提供「α7 V」的单字形补充。
- `main/sony/connection.*` 复用阶段 2 的 `Tunnel` 和 `Session`，通过独立 FreeRTOS 前台会话任务执行网络操作。已保存的指纹再次比对后才发送凭据。指纹变化停在确认页，确认只对当前显示的完整指纹生效。SSH/PTP/IP/SDIO 通过后把配置写到单个 `sony_cam/profile_v1` NVS blob，失败保留上次成功的配置。选择 Wi-Fi 时优先用保存的配置，掉线尝试自动重连，失败可重试或重配。退出取消任务、关闭 socket 和两个通道，并释放网络所有权。反馈音量和触觉复用现有音频、振动接口，独立开关写入 NVS。`tunnel*` 增加取消在途连接的能力。
- `components/esp-wifi-connect` 复用临时热点、DHCP、DNS 和 HTTP Server，加入独立相机表单和真实 STA 扫描。手机辅助页使用短期 WPA2 热点，限制一个客户端，采用表单 POST，提交后停止热点再关联相机。相机配置不写家庭 Wi-Fi 列表。`main/hal/hal_network.cpp`、`hal.h`、`hal_badge.cpp` 管理相机临时网络所有权及原 STA、配置 AP 或关闭策略的恢复。`main/hal/utils/settings/settings.*` 增加显式提交结果。
- `tools/ui-render`、`tools/sony-test/connection_check.cpp` 复用桌面渲染器和协议回环，对生产界面接模拟相机状态，并独立检验共用的凭据解析和键盘逻辑。使用 `python tools/ui-render/export.py --sony-v4` 输出[阶段 3 同源 LVGL 总览](../ui-preview/sony-camera/lvgl-stage3/overview.png)。截图中的相机信息、指纹和临时热点均来自桌面桩。

**构建与桌面验证**

| 检查 | 结果 |
|---|---|
| ESP-IDF 5.5.5 | `./tools/build.ps1 -BuildDirectory build-toolbox` 通过，产物为 `build-toolbox/ToolBoxOS.bin`，分区校验通过。 |
| 同源 LVGL 和输入 | `tools/ui-render/build.cmd` 构建通过。`render.exe` 和 `render.exe full` 通过，覆盖入口、手机辅助状态、指纹变更拒绝、扫描、四类键盘、KEYA/KEYB、保存配置的重连 UI、失败重试及原 BLE 路径。14 个通过字体检查的 466×466 界面及总览均为桌面模拟。实渲染核对了圆屏边界、字体与密码文本区。 |
| 配网输入单测 | `sony_connection_check.exe` 通过，覆盖多击循环、超时、模式切换、字数限制、空格唯一、所有 ASCII 标点的翻页、表单的百分号编码和非法输入。 |
| 通信及原 BLE 回归 | `sony_protocol_check.exe`、`camera_backend_check.exe` 通过。读取 α6700 上游抓样的 352 项属性。固定 alpha2go 模拟服务回环验证指纹不符时送密尝试为 0，错密码拒绝，两个正确凭据会话通过 PTP/IP 双通道、型号/属性和事件读取。 |
| 本轮真实硬件 | 本轮未刷机或推送。手机连接、真实相机应用配网、持久化重连、恢复原 Wi-Fi 和板载资源仍待验收。 |

**构建资源**

- 阶段 3 BIN 为 5,065,440 B，较阶段 2 默认镜像增加 156,864 B，较阶段 2 诊断镜像增加 60,880 B。工厂 APP 分区 12,386,304 B，剩余 7,320,864 B（59.1%）。
- 正式镜像 MAP 中 Flash 段为 4,933,516 B，静态 IRAM 为 16,384 B。共享 D/IRAM 静态占用 150,687 B，较阶段 2 默认镜像增加 1,260 B。其中 `.data` 27,116 B、`.bss` 35,288 B、共享区 `.text` 88,283 B。
- Flash `.text` 与 `.rodata` 口径下，libssh2 占 64,201 B，静态 RAM 692 B。系统 Wi-Fi 组件占 38,483 B，静态 RAM 124 B。相机连接任务、连接 UI、凭据和键盘四个对象共 44,152 B，静态 RAM 497 B。
- 任务栈配置 16,384 B，PTP 缓冲请求 PSRAM 65,536 B。正式 UI 并发的内部 Heap、PSRAM 峰值和任务栈高水位暂无板载数据。

**实机验收待办**

1. 经授权后刷写阶段 3 构建，从应用抽屉进入相机。相机保持在「Wi‑Fi Direct信息」页。分别比较本机扫描和手机辅助两条配网路径，检查输入、热点关闭、真机指纹完整性和 ILCE-7M5 控制台。
2. 完成首次配网后关闭并重开相机应用，确认保存配置重连。验证现场掉线、拒绝变更指纹、主动接受新指纹、网络错误后的重试和重新配置。核对退出后恢复原先的系统 STA、配置热点或关闭状态，并进入原 BLE 页面完成回归。
3. 采集并记录正式 UI 并发时相机任务的 `entry`、`STA`、`SSH`、`ready`、`released` 串口资源。对比最小内部 Heap、最大连续块、最大连续 DMA、PSRAM 及栈高水位。阶段 2 诊断任务的实测数值只代表阶段 2。
4. 凭据放在现有 NVS，当前工程未启用 NVS 或 Flash 加密。临时浏览器连接由 WPA2 热点保护，网页走局部 HTTP。持久化凭据 Flash 静态加密需单独的产品配置和验证。

阶段 4 可接续 `Connection` 持有的串行会话，在同一任务上添加命令与事件交换，把经过实机验证的状态读回接入独立拍照、录像和参数页面。

### 2026-11-25 · 阶段 4 拍照、录像及参数

**前置证据**

- 阶段 2 已用 StopWatch 连接 ILCE-7M5，实测通过 SSH 身份验证、双路 PTP/IP、SDIO v300、属性读取、拍照写卡和录像启停。实测 `D21D` 为 `u8`，待机为 0，录像为 1。停止后 500 ms 的首次读回仍可能为 1。阶段 3 已完成正式连接 UI、两条配网路径和桌面回归，板载配网与网络恢复待验收。本轮继续使用原连接、指纹、凭据和串行会话。

**完成内容与修改文件**

- `main/sony/capture_ui.cpp`、`connection_ui.*` 增加独立拍照、录像就绪、录制中、两类参数列表及相邻值编辑页。页面显示相机报告的模式、曝光、格式、帧率和电量。参数页按场景选择项目，录像页排除照片驱动模式及照片质量。参数由触摸点击或滑动选择，KEYA 放弃，KEYB 或触摸「应用」提交。拍照和录像启停分配给 KEYB。录制中仅通过时间标签更新秒数。
- `main/sony/session.*`、`capture.*`、`connection.*` 在同一个前台任务上调度命令和事件。进入页面、连接及重连读取相机描述符。可写性取自当前描述符，枚举只采用相机列出的可设置项，范围按相机回报的步长使用，显示专用属性只读。确认后重新检查描述符和录像状态，经 `0x9205` 写入，并等待真实读回。相机拒绝或读回未达到所选值时保留相机读回值。连接、切换页面、取消、重连均只读取。
- 拍照复用半按、全按和释放。全按前再次确认录像状态。录像复用单次 MOVIE 按下、释放，按相机读回切换 REC 或就绪。收到属性及会话事件后读取相应状态，合并连续通知。正常情况下每 1 秒补读录像单属性，8 秒补查参数。相机不支持 `0x9251` 时退到较低频率的 `0x9209` 全量读取。启停和写入确认期间有短期读回窗口。断连显示录像状态未知，重连只读取状态。
- `main/apps/app_camera/app_camera.*`、`main/hal/hal.h`、`hal_audio.cpp` 接入公共触摸入口与音频输出锁。准备发出 MOVIE 时先锁触摸并静音，关闭下拉及控制中心，忽略 KEYA 和双键组合。已知正在录制时断连维持锁定，读回待机后恢复。音频层禁止新播放、清除排队数据并启用编解码器静音。有效启停只按反馈配置振动一次。蓝牙分支沿用原实现。
- `tools/sony-test/test.cpp` 增加同源协议和状态测试。`tools/ui-render` 中的 `CMakeLists.txt`、`render.cpp`、`export.py`、`extensions.cpp`、`sony_mock.*` 和 `stubs/hal/hal.h` 接入阶段 4 的模拟状态、按键、触摸和音频断言。`ui-preview/sony-camera/lvgl-stage4/` 保存七页同源 LVGL 截图和[总览](../ui-preview/sony-camera/lvgl-stage4/overview.png)，均为桌面模拟。

**协议与计时边界**

- 继续按固定 alpha2go `02883425` 实现 v300 描述符、单项读取和类型化写入。录像格式标签参照固定 FX30-Controller `85604cbe` 的 v300 线协议记录及 α6700 抓样，帧率等格式化依据公开协议参考。现有 Apache-2.0 归属、参考版本和许可证见阶段 1、2 记录。额外对照了 libgphoto2 `v2.5.32` 与 mikefsq/ptp `v0.1.0` 的属性名和枚举语义，工程无新增依赖。可选项始终从会话描述符生成。 [alpha2go](https://github.com/Knallli/alpha2go/blob/02883425c9445c653b2950210187cdd1547320c2/docs/protocol.md) · [FX30 v300](https://github.com/thiagolegrand/FX30-Controller/blob/85604cbe3930da077616b8cb8f818dbfb45c73fd/docs/RE_FX30_v300.md) · [libgphoto2](https://github.com/gphoto/libgphoto2/tree/v2.5.32/camlibs/ptp2) · [mikefsq/ptp](https://github.com/mikefsq/ptp/tree/v0.1.0/sony)
- 现有 A7M5 证据没有可确认语义的本段录像时长或开始时间。远程开始经相机确认后，用发送 MOVIE 时的单调时钟 +500 ms 估算。机身发起或重连时仍在录制，则标明从检测到录像时计时。计时显示 `HH:MM:SS`。时间码和存储卡剩余录像时长均不作本段计时。未识别的录像状态按未知处理。
- FX30 的上游实测曾出现绝对 ISO 写入返回接受但机身值不同。本阶段依据 A7M5 实际描述符提交明确请求，结果以读回为准。A7M5 各档位、镜头下的绝对写入能力及数值标签仍需机身对照，尤其快门、光圈、ISO、EV、对焦区域及电影格式。缺失的描述符隐藏。已有描述符的未识别枚举以原始代码显示。

**构建与测试**

| 检查 | 实际结果 |
|---|---|
| ESP-IDF 5.5.5 | `./tools/build.ps1 -BuildDirectory build-toolbox` 通过，默认镜像 `build-toolbox/ToolBoxOS.bin`，分区校验通过。保留 ELF、MAP 和烧录参数。 |
| Sony 协议模拟 | `sony_protocol_check.exe` 通过。覆盖 1/2/4 字节标量解析、带符号范围、可设置与仅报告项、显示专用属性、绝对设置数据包和延迟读回、拒绝及繁忙、半按期间机身切换录像、延迟启停、500 ms 估算、单项读取退回全量、机身改参、断线和新会话读取。 |
| 上游抓样离线回放 | 固定 alpha2go 的 α6700 `a6700-9209.bin` 读出 352 项，`D21D` 为只读 `u8`、值 0。此项是上游其他机型样本回放。 |
| Windows SSH 回环模拟 | 错指纹的凭据尝试为 0，错密码拒绝，两个正确凭据会话通过同一固定 libssh2 后端完成双转发、读属性和收事件。 |
| LVGL 桌面模拟 | `tools/ui-render/build.cmd`、`render.exe`、`render.exe full`、`python tools/ui-render/export.py --sony-stage4` 通过。检查上滑选值、KEYA 放弃、明确应用及读回、KEYB 拍照、双次录像启停、全屏触摸和双键锁、录像中的下拉屏蔽、静音与单次振动、断连锁定及重连恢复、时间标签复用。核对七页圆屏、字体和热区。 |
| 配网与 BLE 桌面回归 | `sony_connection_check.exe` 和 `camera_backend_check.exe` 通过，DIRECT 与 FULL 渲染均覆盖原 BLE 页面回归。 |
| 本轮 A7M5 / StopWatch | 本轮未接入真实相机执行阶段 4 测试，未刷机或推送。阶段 2 的实机验证仍是基础会话、拍照及录像启停的证据。 |

**实际构建资源**

- 阶段 3 基线 BIN 在开发前实读为 5,065,440 B。阶段 4 BIN 实读为 5,084,656 B，增加 19,216 B。工厂 APP 分区 12,386,304 B，剩余 7,301,648 B（59.0%）。
- `python -m esp_idf_size` 读取最终 MAP，Flash 段为 4,952,720 B，静态 IRAM 为 16,384 B。共享 D/IRAM 静态占用 150,743 B，较阶段 3 增加 56 B。其中 `.data` 27,116 B、`.bss` 35,344 B、共享区 `.text` 88,283 B。

| MAP 归属 | Flash `.text` + `.rodata`（B） | 静态 RAM（B） |
|---|---:|---:|
| 新增拍摄服务及拍摄 UI | 17,998 | 0 |
| Sony 会话 | 5,847 | 0 |
| Sony 连接及连接 UI | 34,716 | 557 |
| libssh2 | 64,193 | 692 |
| 系统 Wi-Fi | 38,467 | 124 |

- 会话任务栈配置为 16,384 B，PTP 工作缓冲请求 PSRAM 65,536 B。动态属性快照仅保存页面所用的标量和可设置枚举，范围按需计算。本轮暂无运行时内部 Heap、DMA、PSRAM 或栈高水位的板载数据。

**实机验收与衔接**

1. 经授权后在 StopWatch 上完成阶段 3 的两个配网入口、指纹保存、掉线重连及退出恢复网络。相机使用阶段 2 的「Wi-Fi Direct信息」实测路径。使用测试卡，核对照片写卡。若机身选择同时保存至远程目的地，待传输对象可能让 MOVIE 暂时繁忙，按机身设置处理。
2. 对照 A7M5 的固件版本、档位、镜头和机身菜单。分别记录照片、录像就绪和录制中的真实描述符，逐项验证只读/可写、允许值、用户确认、拒绝后的读回、机身改参以及重连无写入。检查照片模式下允许的 MOVIE 启停，确认遥控未改机身模式。
3. 在真实录音或波形中核对启停及录制全过程。覆盖最大音量、操作音开关和振动开关、已打开的系统浮层、录制中 KEYA、双键长按、断线后仍录制及重连才退出锁定。机身发起的录制，触摸与音频锁在会话收到并确认状态后生效。对照机身实测时长和端到端通知延迟。
4. 继续使用现有 `entry`、`STA`、`SSH`、`ready`、`released` 资源日志，在板载操作参数页及连续启停后补采最小内部 Heap、最大连续 DMA、PSRAM 及任务栈高水位。
5. 阶段 5 可在 `Connection` 的同一工作任务上接入机身按键和拨轮，复用录像互斥、触摸反馈及相机状态读回。控制台及拍摄页的相册入口待阶段 6 有真实最新媒体可读时接入，随之保留拍照或录像就绪来源。机身控制和自动化入口仍隐藏。

**2026-11-25 · 用户授权刷写阶段 4**

- 通过 StopWatch 的 USB Serial/JTAG（COM3）将 5,084,656 B 应用镜像写入 factory 分区 `0x20000`。esptool 数据哈希校验通过并发出硬复位。刷写范围仅覆盖应用分区，保留设置和用户存储。相机功能实测待用户验收。

### 2026-11-25 · 阶段 4 实机反馈修复

- 用户报告已在 StopWatch 上通过手机辅助配网连接相机，基本功能正常。手机连接、录制静音、逐项参数写回和重启后的持久化尚无本轮独立采样。此次修复版尚未刷入。
- `main/assets/ui_icons.c`、`ui-preview/app.js` 及两种尺寸的 `settings` 图标共用对称八齿轮廓。控制中心和相机参数入口复用同一资源。
- `main/sony/connection_ui.*`、`capture_ui.cpp`、`capture.cpp` 保留编辑入口来源，从拍照或录像就绪打开快门、光圈、ISO 后，取消或确认读回均返回对应拍摄页。由参数列表进入则返回列表。滚轮到首尾时隐藏空相邻项，拖动跟手并带缓动归位。扫描和参数列表使用公共行缓存，滚动期间延后因相机属性变化引起的列表重建。拍照、录像就绪和录制中复用蓝牙页的电池组件，显示本地时钟及有明确标签的相机电量。
- `main/sony/connection.*`、`components/esp-wifi-connect` 将手机辅助临时热点改为开放网络。沿用单客户端、只在配网期间启用、提交后关闭和机身指纹确认。手机仍需在表单填入相机热点和 SSH 的凭据。早期阶段记录的 WPA2 临时热点对应旧版。
- `tools/ui-render` 补查开放热点提示、滚轮从拖动到归位、首尾字形、页间返回、列表复用及状态栏。DIRECT、FULL、Sony 协议、配网输入与 BLE 桌面回归通过。[修复后 LVGL 桌面模拟总览](../ui-preview/sony-camera/lvgl-stage4/overview.png)
- ESP-IDF 5.5.5 构建与分区校验通过。更新后的 `build-toolbox/ToolBoxOS.bin` 为 5,082,128 B，较已刷入镜像减少 2,528 B，APP 分区剩余 7,304,176 B。MAP 的 Flash 段为 4,950,204 B、静态 IRAM 16,384 B、共享 D/IRAM 静态占用 150,719 B。板载滑动帧数及运行时资源待采样。

### 2026-11-25 · 阶段 4 参数与状态栏修复

- `main/sony/capture.cpp` 按 Sony PTP 线协议修正白平衡名称，包括荧光灯、色温、自定义和水中自动。未核实的枚举保留相机给出的选项编号。RAW 压缩的 `0x07` 按「已压缩 (HQ)」显示，依据 α6700 抓样的 `0x06/0x01` 备选值、两机型手册和用户报告的新增值推定，待在 A7M5 机身菜单逐项核对。两项可选值和发出的值仍来自相机描述符。 [PTP 白平衡参考](https://github.com/gphoto/libgphoto2/blob/v2.5.32/camlibs/ptp2/config.c#L1379-L1390) · [ILCE-7M5 白平衡](https://helpguide.sony.net/ilc/2540/v1/zh-cn/contents/0410C_white_balance.html) · [ILCE-7M5 RAW](https://helpguide.sony.net/ilc/2540/v1/zh-cn/contents/251h_raw_file_type.html) · [ILCE-6700 RAW](https://helpguide.sony.net/ilc/2320/v1/en/contents/0404I_raw_file_type.html)
- `main/sony/capture_ui.cpp`、`connection_ui.*` 让连续滑动跨越多个相机允许值，复用五条滚轮标签并在松手后对齐。等待确认期间相机保持原参数。拍照、录像和录制状态栏区分本机及相机电量，本机按实际外部供电状态显示闪电图标。控制台标题显示连接会话读出的型号。
- `tools/sony-test/test.cpp` 和 `tools/ui-render/render.cpp` 检查白平衡、RAW 标签、上下各跨三档、确认前无写入、供电图标原位更新、型号和字形。DIRECT、FULL、Sony 协议、α6700 上游抓样回放、配网及 BLE 桌面回归通过。[LVGL 桌面模拟总览](../ui-preview/sony-camera/lvgl-stage4/overview.png)
- ESP-IDF 5.5.5 构建和分区校验通过。新 BIN 为 5,082,960 B，较上一版增加 832 B，APP 分区剩余 7,303,344 B。MAP 的 Flash 段 4,951,028 B、静态 IRAM 16,384 B、共享 D/IRAM 静态占用 150,679 B。本轮未刷机，A7M5 枚举标签与板载交互待实测。

### 2026-11-25 · 阶段 5 机身控制

**前置证据**

- 阶段 2 已在 A7M5 实测 SSH、双路 PTP/IP、SDIO v300、`GetDeviceInfo`（`ILCE-7M5`），扩展清单公布 484 个属性、60 个控制，记录了快门和 MOVIE。清单中是否含 `0xD309`/`0xD30B`，以及 `0xD208`/`0xD20A` 的实际 ID，当时没有记录。
- 阶段 3 的两条配网入口、指纹保存和网络恢复，阶段 4 的逐项参数读回、录像静音和板载资源仍待专项实机验收。用户已确认阶段 4 手机辅助连接和基本功能正常。本轮复用 `Connection` 的串行工作任务、录像锁、触摸锁和反馈设置，没有改动前置阶段的协议或页面行为。

**完成内容与修改文件**

- `main/sony/session.*`：从 SDIO 控制清单识别 `0xD309` 按键和 `0xD30B` 拨轮。每次全量读取时，从 `0xD208`/`0xD20A` 的可设置枚举取可用 ID，`enabled` 为 0 时视为当前不可用。`pressButton` 用 `0x9207(0xD309)` 发送 LE `u32` 的 `ID<<16|2` 按下，随后总是发送 `ID<<16|1` 释放。即使相机拒绝按下，也照样发送释放。释放被拒时重试一次，仍失败则将会话标记为失败并走重连，这与快门释放的处理一致。`turnDial` 用 `0x9207(0xD30B)` 发送 `ID<<16|int16 步数`，正值为顺时针。相机未列出的 ID 返回 Unsupported，不发送任何数据。
- `main/sony/body.*`：只提供 α6700 参考映射中的方向 1–4、SET 5、MENU 6、Fn 0x10、回放 0x11、C1–C3 0x14–0x16、AF-ON 0x1C，以及控制转盘 0x4001、前拨轮 0x4002、后拨轮 0x4003，并要求相机同时列出。MOVIE 0x1A 不走机身通道，录像只经过静音和触摸锁定的拍摄路径。
- 同一文件中的 `BodyQueue` 最多保存 8 条命令，按 FIFO 执行。同一拨轮的连续转动合并为一条，净步数为零的命令直接丢弃，等待超过 1500 ms 的命令丢弃，队列满时拒绝新输入。
- 同一文件中的 `WheelGesture` 以 15° 为一步。从圆心 46 px 内按下视为 SET，圆盘半径为 109 px。只有累计转角小于 8°、位移不超过 24 px 的触摸才算点按。转动后松手不会触发方向键，靠近圆心时忽略角度抖动。
- `main/sony/connection.*`：命令队列放在 `Runtime` 中。工作任务每轮先执行拍摄请求，再取一条机身命令，然后处理事件和读回。队列不空时跳过 30 ms 等待。录像锁定、断线、退出应用和离开机身页面时清空队列。会话状态中发布相机列出的 ID 和最近一次结果，界面不等待网络。
- `main/sony/body_ui.cpp`、`connection_ui.*`：控制台按相机能力显示「机身」入口。机身控制页按 V4 布局显示 MENU、Fn、融合控制转盘（深色环、四方向标识、中央 SET、冰蓝指示线）以及回放和拨轮入口。拨轮页包含前后拨轮的 −/+、可横滑的拨轮条和 C1/C2/C3/AF-ON。
- 交互方面，转盘每步转动 15° 指示线，−/+ 单击发送一步，长按连续步进。拨轮条每横滑 28 px 发送一步，向右为正。机身页 KEYB 等同 SET，拨轮页 KEYB 不执行动作也不反馈。KEYA 逐级返回，并清空未执行的命令。
- 每条被接受的指令复用现有反馈接口，给出一次音效和振动，同样遵守音效开关、系统音量和振动开关，录像静音优先。相机忙或拒绝时显示提示，不伪装成功。
- 刷新方面，转盘只更新弧线角度，拨轮条只移动条纹，不整屏重绘。回放图标用 LVGL 绘制的边框和三角形，没有使用图片。
- `tools/sony-test/test.cpp`、`tools/ui-render/{render.cpp,sony_mock.*,CMakeLists.txt,export.py,stubs/hal/hal.h}` 增加协议、队列、手势和 LVGL 交互测试。桩中的扬声器音量改为可设置。`ui-preview/sony-camera/lvgl-stage5/` 保存控制台、机身控制和拨轮页的同源 LVGL 截图及[总览](../ui-preview/sony-camera/lvgl-stage5/overview.png)。截图中的按键列表来自桌面模拟。

**协议依据与边界**

- 协议依据为固定 alpha2go `02883425` 的 `ptpip/sonycontrols.go`（`SonyPressButton`、`SonyTurnDial`、控制码与状态属性表）和 `docs/protocol.md` 中的按键、拨轮表，许可证归属见前述阶段。按键和拨轮 ID 是 α6700 的上游实测结果。用本工程的会话代码回放上游 α6700 `a6700-9209.bin`，读出 `D208` 为 `0x1–0x6, 0x10, 0x11, 0x14–0x16, 0x1A, 0x1C`，`D20A` 为 `0x4001–0x4003`，两者都是 `u16`、`writable=0`、`enabled=1`。这些只是其他机型的样本证据。
- 所有按键都按单击发送：按下后立即释放。AF-ON 和自定义键不模拟按住，按住语义需按实机行为再决定。没有用 `0xD20C`（空闲）拦截发送，原因是 A7M5 的语义未验证，相机的拒绝码会显示给用户。上游记录过在 α6700 上用 `0xD309` 操作菜单后曝光模式发生变化，A7M5 需要实机确认。
- 执行中的按下和释放在同一次工作调用内完成。若隧道在两者之间中断，依赖相机在会话关闭后释放按键，这一点需实机确认。

**构建与测试**

| 检查 | 实际结果 |
|---|---|
| ESP-IDF 5.5.5 | `./tools/build.ps1 -BuildDirectory build-toolbox` 编译、链接和分区校验通过，产物为 `build-toolbox/ToolBoxOS.bin`。新增的 Sony 源文件没有编译警告。 |
| Sony 协议模拟 | `sony_protocol_check.exe <alpha2go>/ptpip/testdata/a6700-9209.bin` 通过。新增用例覆盖：控制清单缺失或 `enabled=0` 时无按键且不发送、`D309` 按下/释放的线格式与顺序、按下被拒仍释放、释放两次被拒后会话失效、`D30B` 的 −1 编码为 `0x4001FFFF`、+3 编码为 `0x40020003`、未列出 ID 不发送。 |
| 队列与手势模拟 | 同一程序检查 FIFO、同轮合并、净零丢弃、容量 8、1500 ms 过期、MOVIE 排除。手势覆盖四向和 SET 点按、顺时针 50° 发 +3、逆时针 35° 发 −2、跨 ±180° 接缝、整圈 24 步、转动或 12° 来回摆动后松手不触发方向、径向拖动不算点按。 |
| 上游抓样回放 | α6700 352 项完整读出，按键和拨轮列表如上。这是其他机型的样本。 |
| LVGL 桌面模拟 | `cmake --build build-ui-render --clean-first`、`render.exe`、`render.exe full`、`python tools/ui-render/export.py --sony-stage5` 通过。转动 50° 发出 3 个 +1、零个方向；逆时针发出 −2；四向和中心点按依次发出 1/4/2/3/5；KEYB 发出 SET；MENU、Fn、回放分别发出 6/0x10/0x11。每条命令振动一次，音量为 0 时无音但仍振动。忙碌提示、拨轮页 KEYB 无动作无反馈、前拨轮 ±1、长按后拨轮连续 3–4 步且松手不多发、横滑 90 px 发 +3 和 70 px 发 −2、C1/C3/AF-ON、KEYA 清队列返回均已检查。相机只列出部分 ID 时仅显示这些控件，未列出的方向不发送。机身页中机身发起录像会立即锁定并屏蔽转盘和 KEYA，回到就绪后恢复。相机不再列出任何控件时隐藏入口。机身控制不产生拍摄请求。截图核对了圆屏边界、字体和热区。 |
| 既有回归 | `sony_connection_check.exe`、`camera_backend_check.exe` 通过，DIRECT 与 FULL 渲染覆盖阶段 3/4 的 Sony 页面和原 BLE 页面。固定 alpha2go 模拟服务的 SSH 回环通过：错指纹时凭据尝试为 0，错密码被拒，两个正确会话完成认证。 |
| 本轮 A7M5 / StopWatch | 没有接入相机，未刷机，未推送，未提交。 |

桌面构建注意：本机 MSVC 输出中文 `/showIncludes` 提示，Ninja 无法据此记录头文件依赖。修改头文件后，必须用 `--clean-first` 重建 `build-ui-render`，否则旧对象会链接到新的结构布局。本轮先出现过一次因此导致的旧用例假失败，清理重建后通过。

**实际构建资源**

- 开发前实读的阶段 4 BIN 为 5,082,960 B。阶段 5 BIN 为 5,091,168 B，增加 8,208 B。工厂 APP 分区 12,386,304 B，剩余 7,295,136 B（58.9%）。
- `python -m esp_idf_size` 读取最终 MAP：Flash 段 4,959,236 B，静态 IRAM 16,384 B，共享 D/IRAM 静态占用 150,815 B（`.data` 27,116 B、`.bss` 35,416 B、共享区 `.text` 88,283 B），较上一版增加 136 B。

| MAP 归属 | Flash `.text` + `.rodata`（B） | 静态 RAM（B） |
|---|---:|---:|
| 机身命令、队列和手势 `body.cpp` | 1,518 | 0 |
| 机身控制和拨轮 UI `body_ui.cpp` | 3,926 | 0 |
| Sony 会话 `session.cpp` | 6,526（+679） | 0 |
| Sony 连接及连接 UI | 35,336（+620） | 625（+68） |

- 没有新增任务或缓冲。命令队列为 8×8 B，计入连接对象的 `.bss`。会话对象增加约 100 B 的 ID 表，位于 16,384 B 的相机工作任务栈上。机身状态只保存相机列出的少量 ID。本轮没有板载 Heap、PSRAM、DMA 或栈高水位数据。

**实机验收待办**

1. 经授权刷写后连接 A7M5，记录扩展控制清单是否包含 `0xD309`/`0xD30B`，以及 `0xD208`/`0xD20A` 的实际 ID、类型和 `enabled`。缺失时相应控件保持隐藏。
2. 逐个按下 MENU、Fn、回放、四方向、SET、C1–C3、AF-ON，核对机身动作是否与 α6700 映射一致，并确认没有残留按下状态。不一致时按实测修正映射。记录 `0xD20C` 的变化和忙碌返回码。
3. 在照片 M 档和菜单内分别操作控制转盘、前后拨轮，核对正负方向、每步对应 1 格，以及 15° 步进的手感，必要时调整步进角。检查快速转动时的合并、超时丢弃和无乱序，并检查菜单操作是否改变曝光模式。
4. 测试执行中断线、重连后不重放命令、机身发起录像后的锁定，以及最大音量、音效关闭和振动关闭时的反馈。
5. 使用现有 `entry`、`STA`、`SSH`、`ready`、`released` 资源日志，在连续转动和按键后补采内部 Heap、PSRAM、DMA 和任务栈高水位。

**阶段 6 衔接**

控制台的「相册」入口按 V4 预留在 (238, 254)。媒体列表、缩略图和删除应放在同一工作任务上，优先级排在拍摄请求和机身命令之后，以满足「控制命令优先于预加载」。离开媒体页时同样清空未执行的请求。阶段 6 继续使用阶段 1 记录的 `0x923B/923C/923E` 及删除命令，并在 A7M5 上验证。

### 2026-11-25 至 26 · 阶段 6 媒体管理

**前置验收**

- 复用阶段 2 的实机结果。A7M5 已验证 Wi-Fi Direct、SSH 指纹和认证、双 PTP/IP 通道、SDIO v300、属性读取、拍照和录像启停。阶段 4 手机辅助连接及基本功能已有用户实机反馈。阶段 3 的各配网与恢复路径、阶段 4 的专项实机验收、阶段 5 的 A7M5 机身控制仍按各自清单执行。此次只在桌面与构建环境验收媒体。
- 协议沿用阶段 1 锁定的 alpha2go `02883425c9445c653b2950210187cdd1547320c2` 和已有 Apache-2.0 归属。仅核对已知媒体操作、α6700 的数据布局和事件语义。 [上游媒体代码](https://github.com/Knallli/alpha2go/blob/02883425c9445c653b2950210187cdd1547320c2/ptpip/sonyremote.go) · [上游协议记录](https://github.com/Knallli/alpha2go/blob/02883425c9445c653b2950210187cdd1547320c2/docs/protocol.md#contents-0x923b0x923e)

**完成内容与修改文件**

- `main/sony/media.cpp`、`media.h`、`session.*` 从 `GetDeviceInfo` 启用相机公布的媒体能力。用 `0x923B` 判定有目录的卡槽，通过 `0x923C` 合并两槽并按创建 UTC 排序。每次取一页元数据，检测卡住的游标和冲突的对象 ID。用 `(卡槽, Content ID)` 标识内容，用 `(卡槽, Content ID, File ID)` 请求 `0x923E` 静态缩略图。相同文件名和相同 Content ID 可以分属两张卡。
- `main/sony/media_image.cpp` 复用仓库 M5GFX 的 TJpgDec，在会话任务上将基线 JPEG 转为 RGB565，按原比例显示。压缩图超过工作缓冲时按有界方式跳过，HEIF 和拒绝读取采用占位。缓存最多当前窗口的四项，单张回看请求当前和最多两项相邻缩略图，列表只请求当前 2×2 页。
- `main/sony/media_ui.cpp`、`connection_ui.*`、`capture_ui.cpp` 实现控制台、拍照、录像就绪三处直达最新媒体，复用 `body_ui.cpp` 的回放图标。单张支持触摸方向键、横滑及列表切换。列表支持横滑翻页和 KEYB 查看。相机路径中取真实文件名，静态照片采用目录的本地创建时间显示 `YYYY/MM/DD HH:MM:SS`，用 UTC 和本次进入时的设备时间计算相对时间。切换、等待、取消和删除均沿用同一时间基准。未来时间及无效时钟不生成相对时间。
- 影片用灰色 `#A0A0A0` 三角形表示。目录确认存在影片参数块，固定参考未解出真实起始时间和时长。影片按相机目录的创建 UTC 参与排序，界面显示拍摄时间不可用，时长留空。该排序在 A7M5 上仍需与机身回放对照。
- `main/sony/connection.*`、`capture.*` 复用同一前台会话任务，处理完拍摄、机身请求和事件后再执行一个媒体事务。离开媒体、录像锁定、退出应用和断线使媒体代次失效。界面保留缩略图数据到 LVGL 图像对象和缓存释放，已显示图片异步到达时只更新对应区域。
- 删除页显示真实文件名、文件类型、卡槽和整组范围。KEYA 或触摸取消，KEYB 或触摸删除才提交。发送 `0x9250(Content ID, slot)` 前重读目标卡目录，核对时间、File ID、路径、格式、长度、UMID 和所有配对文件，保护项不开放删除。应答成功后重读目录核实，同时处理 `0xC240`。槽位从目录消失且无法区分空卡与移除时，需要匹配同卡同内容的事件才能确认。删除拒绝显示原因并保留原项，结果未确认单独显示，确认成功后清理缩略图并切至相邻媒体或空相册。
- `tools/sony-test/test.cpp`、`tools/ui-render/{render.cpp,sony_mock.*,CMakeLists.txt,export.py}` 增加媒体线协议、身份与范围、时间及同源 LVGL 测试。预览使用仓库 LVGL 示例 JPEG 和模拟目录。

**构建与测试**

| 检查 | 实际结果 |
|---|---|
| ESP-IDF 5.5.5 | `./tools/build.ps1 -BuildDirectory build-toolbox -IdfPath <ESP-IDF 5.5.5 路径>` 编译、链接及 APP 分区校验通过，输出 `build-toolbox/ToolBoxOS.bin`。沙箱内启动已安装的交叉编译器遇到 WinError 623，自动审批允许在沙箱外使用同一工具链完成构建。 |
| PTP/IP 与媒体模拟 | `build-ui-render/sony_protocol_check.exe` 通过。覆盖双卡同名同 Content ID 的索引和最新排序、File ID 与卡槽在线协议中的编码、RAW+JPEG 范围变化后拒绝发出删除、授权撤销、相机忙、成功重列仅移除目标槽、末项和空卡、同号异槽事件无法确认缺失的目标卡、卡住的游标、截断元数据、超出缓冲的缩略图、秒/分/小时/天与时区字段。 |
| LVGL 桌面模拟 | 清理后用 `tools/ui-render/build.cmd` 重建，在 `build-ui-render` 运行 `render.exe` 和 `render.exe full` 均通过。包含三处相册入口、横滑与方向键、4 项缩略图窗口、列表 KEYB 和触摸、删除确认/取消、拒绝、成功后的相邻项和空列表、停留时相对时间保持、退出重进重新计算。使用生产 JPEG 解码器实际解码示例，核对灰色播放标识、原比例预览及圆屏排版。 |
| 既有桌面回归 | `sony_connection_check.exe`、`camera_backend_check.exe` 通过。DIRECT 和 FULL 渲染同时运行既有连接、拍摄、机身控制和原 BLE 流程。 |
| A7M5 / StopWatch | 本轮未连相机或开发板。无媒体实测、板载资源样本或真实删除操作。未刷机、提交或推送。 |

`python tools/ui-render/export.py --sony-stage6` 输出 [九张页面截图及总览](../ui-preview/sony-camera/lvgl-stage6/overview.png)。目录、时间、删除结果均来自桌面模拟。

**实际构建资源**

- 开发前阶段 5 BIN 为 5,091,168 B。阶段 6 BIN 为 5,140,784 B，增加 49,616 B。工厂 APP 分区 12,386,304 B，余 7,245,520 B（58.5%）。
- `python -m esp_idf_size` 读取最终 MAP 得到 Flash 5,008,848 B（较阶段 5 增加 49,612 B），静态 IRAM 16,384 B，共享 D/IRAM 静态占用 150,959 B（增加 144 B，`.data` 27,116 B、`.bss` 35,560 B、共享区 `.text` 88,283 B）。

| MAP 归属 | Flash `.text` + `.rodata`（B） | 静态 RAM（B） |
|---|---:|---:|
| 媒体协议、目录及工作状态 `media.cpp` | 28,272 | 0 |
| 媒体 UI `media_ui.cpp` | 10,433 | 0 |
| RGB565 解码适配 `media_image.cpp` | 724 | 0 |
| 既有 M5GFX TJpgDec `lgfx_tjpgd.c` | 3,933 | 0 |
| Sony 会话 `session.cpp` | 6,937 | 0 |
| Sony 连接及连接 UI | 39,538 | 769 |

- 复用 16,384 B 的会话任务栈和 65,536 B 的 PTP 工作缓冲。解码器临时请求 4,096 B PSRAM。每张缩略图最多 352×256×2 B，四张缓存的像素上限为 720,896 B，界面换图期间旧帧可能持有引用。目录和扫描中的元数据动态分配。这些是配置和容量上限，板端运行时占用待实机采样。

**实机验收与阶段 7 衔接**

1. 经授权刷写后，读取 A7M5 公布的 `0x923B/0x923C/0x923E/0x9250` 能力，逐项核对单卡、双卡、空卡、相机忙、切卡、分页及同名文件，检查最新排序和机身文件名。当前最多索引 8192 项元数据，超出或游标停滞显示目录读取失败。需实测大量同毫秒记录跨页时的相机游标行为。
2. 对照相机拍摄信息检查 JPEG、RAW、RAW+JPEG、HEIF 及影片的缩略图支持、日期、时区、设备时钟偏差及影片实际开始时间、时长的来源。现阶段只请求 Thumbnail。验证设备显示「30 秒前拍摄」后停留及往返保持固定，完全退出重进重新计算。
3. 只有用户指定可丢弃的测试媒体后才做真实删除。双卡安排同名文件或同号内容，记录 `slot / Content ID / File ID / UMID` 与操作前后卡内目录。验证确认的 RAW 和 JPEG 是否一起消失、另一槽及相邻对象是否保留，检查确切的 `0xC240` 参数、最后一项、保护项、相机忙、拔卡和断线。当前没有 A7M5 实际删除证据。
4. 在 StopWatch 上记录 `media-index`、`media-thumbnail`、`released` 日志，测量双卡及 2×2 翻页的内部 Heap、PSRAM、DMA 和栈高水位。验证读取中机身发起录像时的锁定、退出后任务释放及网络恢复。
5. 阶段 7 可复用现有 `Connection` 串行任务、拍摄读回和参数能力，继续间隔拍摄与预设。焦点堆栈入口须等待机身和镜头支持的证据。

### 2026-11-26 · 阶段 6 使用反馈修正

- 连续删除时，上一笔删除的 `Deleted` 状态仍随相邻项的缩略图更新发布，原界面遇到该状态就退出确认页。现在确认页仅在本页已提交删除、且这笔请求从核对或删除状态进入成功时自动返回。缩略图更新可继续显示在确认页。原卡槽、Content ID、RAW+JPEG 范围检查继续用于每一笔独立提交。
- `main/sony/media.cpp` 使用 `0x923C` 已读出的本地创建时间和 UTC。照片仍显示拍摄日期和相对时间。影片明确标注「创建」和「前创建」，本地字段缺失时以 `UTC` 明示回退。两种时间均使用进入回看时固定的基准。影片真实录制开始时间及片长仍待可验证元数据。 [固定版本 0x923C 解析](https://github.com/Knallli/alpha2go/blob/02883425c9445c653b2950210187cdd1547320c2/ptpip/sonyremote.go) · [Sony SDK 内容时间码说明](https://amarburg.github.io/sony_remote_camera_sdk/tips_trouble_shooting/tips_trouble_shooting.html#time-code-specifications-of-content-info)
- `main/sony/media_ui.cpp`、`connection_ui.*` 将旧项放左边。单张横滑右移看旧项，左移看新项。相册上滑进入下一页，下滑返回上一页，每次最多一页。两处均移除翻页箭头，滑动时移动预先布置的 LVGL 面板，松手后缓动吸附，短滑或超边界回弹。相册在落页后才请求该页缩略图，仍使用四项缓存。
- 回看使用高对比的返回与相册按钮及带文字的删除按钮，放大确认与取消热区。缩略图分别显示「正在读取」和「无预览」。

**回归与资源**

- `tools/ui-render/render.cpp` 在 DIRECT 和 FULL 均验证两次连续删除，包含第二个确认页收到第一笔成功状态及迟到缩略图、第二次 KEYB 才提交、第二次成功后正确选中相邻项。横滑检查按住时坐标跟手、短滑回弹、松手缓动和旧左新右。竖滑检查按住时坐标跟手、长滑最多一页、落页才加载、错误轴屏蔽和点击选中。DIRECT 模式两种跟手步骤均局部重绘。导出更新后的[桌面截图](../ui-preview/sony-camera/lvgl-stage6/overview.png)。
- `tools/sony-test/test.cpp` 增加影片本地创建日期、相对创建时间、UTC 回退和无效时间测试。`sony_protocol_check.exe`、`sony_connection_check.exe`、`camera_backend_check.exe` 均通过。
- ESP-IDF 5.5.5 用 `tools/build.ps1 -BuildDirectory build-toolbox` 构建及分区校验通过。修订 BIN 5,142,656 B，较首版阶段 6 增加 1,872 B。工厂 APP 分区余 7,243,648 B。最终 MAP Flash 5,010,728 B，共享 D/IRAM 静态占用 150,959 B，静态 IRAM 16,384 B。未扩大网络缩略图缓存。滑动只移动两个可见面板，不读相机或解码。
- 本轮测试为桌面模拟和构建验证。StopWatch 触摸跟手和帧时间、A7M5 连续删片及视频创建时间与机身回放的对照待实机验收。沿用阶段 6 的权限和测试范围，未刷机、提交或推送。

### 2026-11-26 · 阶段 6 回看提速及异步操作

- `main/sony/connection.*`、`media.*` 在拍照及录像就绪页预读相机目录和一张最新缩略图，回看直接使用已有快照。相机拍摄完成或内容事件触发后台重读，保留身份匹配的原缩略图。目录更新后，用户尚未切换时可接续显示真正的最新项。无预读快照时显示读取状态。退回拍摄页保留缓存，离开拍摄和浏览范围后释放缓存。相机任务用通知立即响应新请求，媒体事务之间让出一个调度节拍，仍优先执行相机控制和处理事件。
- `main/sony/media_ui.cpp`、`connection_ui.*` 确认删除后立即隐藏待处理项、关闭确认页并切到相邻项。最多可排队四笔显式确认的删除，每笔保存单个媒体对象，由同一会话串行核对卡槽、对象 ID 和全部配对文件。确认成功才从相机目录移除目标，拒绝或结果无法确认时显示结果，仍在可访问目录中的对象恢复显示。退出应用和进入录制锁定会取消尚未发送的媒体任务。
- `main/sony/capture_ui.cpp` 应用参数后立即返回原页，原值继续显示到真实读回。快门、机身控制及拨轮提交后给出本地反馈。录像启停仍用真实录制状态维持计时、静音和触摸锁。
- `tools/ui-render` 的 DIRECT 和 FULL 回归验证快门结果尚未到达时仍可打开已预读的相册、首屏即显示缓存文件、两笔删除在首笔回包前可依次提交、单笔拒绝后恢复原项、参数应用立即返回及稍后读回。Sony PTP/IP、配网和原 BLE 后端回归通过。板载首张加载耗时和真实相机反馈时序仍待验证。
- ESP-IDF 5.5.5 编译、链接和分区校验通过。`build-toolbox/ToolBoxOS.bin` 为 5,147,456 B，较上版增加 4,800 B，工厂 APP 分区余 7,238,848 B。MAP Flash 为 5,015,532 B，共享 D/IRAM 静态占用为 151,031 B（增加 72 B），静态 IRAM 为 16,384 B。网络缩略图缓存仍为四项，预读时只请求最新一项。未实测板载峰值内存，未刷机、提交或推送。

### 2026-11-26 · 阶段 7 自动化摄影

#### 前置验收核对

- 阶段 2 已在 A7M5 与 StopWatch 实测 Wi-Fi Direct、SSH 指纹及认证、双 PTP/IP 通道、SDIO v300、读属性、单次拍照写卡和录像启停。阶段 4 的手机辅助连接和基本功能有用户实机反馈。阶段 3 的配网与网络恢复、阶段 4 的参数和录像专项、阶段 5 的机身按键及拨轮、阶段 6 的媒体能力和删除范围仍有实机验收待办。
- 复用阶段 1 记录的 Alpha-Fairy `fd1f2989`（MIT）前台间隔任务和镜头步进限制，以及现有 alpha2go `02883425`（Apache-2.0）媒体协议移植。本轮无新增上游源码或依赖。

#### 完成内容和修改文件

- 间隔和张数可设为 1–999 秒、1–999 张。相机须公布快门、单张驱动和能够读出卡内内容的接口。KEYB 启动，KEYA 停止，再按返回。每次只下发一次快门序列，发出半按及全按前分别重新核对相机状态、单张驱动和取消权限。全按后至少等待已设间隔，且上一张得到写卡确认后才继续。
- 首次拍摄前用现有媒体索引记录两槽内容。其后用创建时间游标和 Content ID 确认新增照片对象，保留时间边界以区分同毫秒对象。RAW+JPEG 同一对象计一张。首张确认后固定使用该槽核对，避免双卡副本重复计数。快门应答本身不增加计数。120 秒未确认或同一槽出现多个候选时停止。运行中勿在机身同时触发快门，现有媒体记录缺少将物理快门和遥控快门区分开的事务关联。
- 预设使用 `sony_cam/preset_v1` 保存一份相机刚读回的可写参数，关联机型及机身模式。确认页展示保存值，KEYA 取消，KEYB 或触摸「应用」明确授权写入。逐项核对相机当前类型与允许值并读回，跳过不可选值，遇拒绝或读回不符停止后续写入。结束再读回，显示匹配项数。连接、重连、查看及切换模式只读取。取消或离开时停止继续下发，已进入传输的操作可能已被相机执行。
- 普通退出、A+B 返回表盘、掉线及录制锁定取消后续自动化指令。会话不在重连后重放任务。运行期间保持前台屏幕活动，沿用非录像下拉和按键交互。
- Sony 帮助指南确认机身菜单支持焦点包围。现有证据未确认目标镜头及当前远程会话能设置步进并执行拍摄，焦点堆栈入口隐藏。 [Sony A7M5 焦点包围](https://helpguide.sony.net/ilc/2540/v1/en/contents/211h_focus_bracket.html)
- `main/sony/automation.{h,cpp}` 和 `automation_ui.cpp` 承担调度、核对及 LVGL 页面。`connection{,_ui}.{h,cpp}` 接入现有工作任务、NVS、前台退出和按键。`capture.{h,cpp}`、`session.{h,cpp}`、`media.{h,cpp}` 复用状态读回、快门及目录解析。`tools/sony-test/test.cpp`、`tools/ui-render/{CMakeLists.txt,render.cpp,sony_mock.*,export.py}` 增加同源模拟验证。

#### 桌面模拟与回归

| 检查 | 结果 |
|---|---|
| `sony_protocol_check.exe` | 通过。验证计时回绕、无新对象时不计数、RAW+JPEG 成组计数、确认前不续拍、半按取消并释放、超时停止。预设模拟验证保存时重新读取、加载不写入、机身模式不匹配时不写入、跳过不允许值及真实协议读回流程。 |
| `sony_connection_check.exe`、`camera_backend_check.exe` | 配网表单、九宫格及原 Sony BLE GATT 后端的桌面回归通过。 |
| `tools/ui-render/build.cmd`、`render.exe`、`render.exe full` | MSVC 构建与 DIRECT、FULL 两种同源 LVGL 模拟通过。检查入口隐藏、触摸调整、KEYB 防重复启动、计数标签局部更新、KEYA 停止和 A+B 退出、重连不重放、预设取消及明确应用、部分匹配结果。 |
| `python tools/ui-render/export.py --sony-stage7` | 导出七张 466×466 的 LVGL 页面截图和总览，核对圆屏、字体、热区与单位。截图中的参数和计数均为桌面模拟。 [阶段 7 LVGL 桌面截图](../ui-preview/sony-camera/lvgl-stage7/overview.png) |

本轮未连接真实相机或 StopWatch 执行阶段 7 测试。A7M5 上的目录响应时序、间隔与计数、板载持久化及目标镜头能力仍待核对。未刷机、提交或推送。

#### 固件构建与实际资源

- ESP-IDF 5.5.5 使用已有环境运行 `tools/build.ps1 -BuildDirectory build-toolbox`，编译、链接及分区校验通过。Windows 沙箱内工具链启动返回 WinError 623，使用同一本地工具链在沙箱外完成构建。
- 开发前实读阶段 6 BIN 5,147,456 B。最终 `build-toolbox/ToolBoxOS.bin` 5,166,048 B，增加 18,592 B。工厂 APP 分区 12,386,304 B，余 7,220,256 B（58.3%）。
- 用 `python -m esp_idf_size` 读取最终 MAP。Flash 5,034,112 B，增加 18,580 B。静态 IRAM 16,384 B，共享 D/IRAM 静态占用 151,095 B，增加 64 B。其中 `.data` 27,116 B、`.bss` 35,696 B、共享区 `.text` 88,283 B。

| MAP 归属 | Flash `.text` + `.rodata`（B） | 静态 RAM（B） |
|---|---:|---:|
| 自动化工作逻辑 `automation.cpp` | 5,782 | 0 |
| 自动化页面 `automation_ui.cpp` | 6,286 | 0 |
| 连接与连接 UI | 46,999 | 905 |
| 快门会话与属性控制 `session.cpp`、`capture.cpp` | 17,280 | 0 |
| `libmain.a`（整组件，含上述文件） | 2,701,433 | 9,444 |

- 复用原有 16,384 B 会话任务栈和 65,536 B PTP 缓冲。间隔拍摄开始时的目录扫描复用现有 8,192 项上限，不加载缩略图。此后单次仅解析一页增量元数据。板载运行时的内部 Heap、PSRAM、DMA 与栈余量暂无实测。

#### A7M5 实机待验收与阶段 8 衔接

1. 以测试卡核对单卡、双卡备份、照片与 RAW 分槽、空卡、写入繁忙及长曝光。逐张核对 A7M5 的目录、实际间隔和屏上计数，确认同毫秒、已满或拔卡时的行为。
2. 在单张和非单张之间切换、录制开始、掉线和重连时检查中止。分别在半按期间、等待写卡以及两次曝光之间测试 KEYA 和 A+B，核对快门释放及退出后无续拍。
3. 分别保存照片和录像参数，重启检验单份预设持久化。改用其他模式、改变镜头或允许值、取消确认及应用到一半退出时，核对相机真实读回和写入范围。
4. 采集 `automation-start`、`automation-end` 与 `released` 日志，核对最大目录时的内部 Heap、PSRAM、DMA 和任务栈高水位。阶段 8 可据此完成整体验收及前置实机待办。焦点堆栈仅在已有受支持接口和镜头证据时进入实机验收范围。

### 2026-11-26 · 阶段 8 性能优化及整体验收

#### 前置验收核对

- A7M5 实机证据仍以阶段 2 为基础：Wi-Fi Direct、SSH 指纹及认证、双路 PTP/IP、SDIO v300、`ILCE-7M5` 型号与属性、单次拍照写卡、录像启停与 `D21D` 读回。阶段 4 有用户在正式 UI 上的手机辅助连接及基本功能反馈。
- 阶段 3 的本机键盘与网络恢复、阶段 4 的逐项参数和录像静音、阶段 5 的按键与拨轮 ID、阶段 6 的媒体与删除范围、阶段 7 的间隔与预设仍只有桌面模拟证据，已合并进 [阶段 8 实机验收清单](SONY_CAMERA_STAGE8.md#6-a7m5-实机验收清单建议顺序)。
- 本轮开始时实测：`build-toolbox/ToolBoxOS.bin` 增量构建通过，5,166,048 B，与阶段 7 记录一致。桌面四个检查程序在原代码上全部通过。

#### 完成内容与修改文件

- **性能**：`main/sony/connection.{h,cpp}` 新增 `Connection::revision()`。`connection_ui.cpp` 的 `update()` 先比对修订号，工作任务发布新状态后才复制 `ConnectionState`。原实现在 1 kHz 主循环每轮持锁复制字符串、相机列表和机身 ID 向量，并与会话任务争用互斥锁。
- **UI 统一**：`connection_ui.{h,cpp}` 新增共享圆形返回键 `cornerButton()` 和确认按钮尺寸常量，按 V4 为禁用按钮加 40% 不透明度，控制台入口改为 `drawConsoleTiles()` 顺序填格。`body_ui.cpp` 调整 MENU/Fn 避开返回键，提供共享的绘制 −/+ `signGlyph()`，机身与回放图形改为 30 px 框，自定义键行上移。`capture_ui.cpp` 下移参数列表，控制台拍照、录像改为 30 px 绘制图形。`media_ui.cpp` 采用共享返回键与镜像相册键，重排回看页，状态提示改为预览底部浮层，删除确认采用统一按钮。`automation_ui.cpp` 统一步进键、列表位置、预设确认行样式和按钮，去掉与按钮重复的「KEYB 应用」。
- **复查无需修改**：会话任务独占 `Session`，命令优先于事件、事件优先于媒体预读；退出取消任务并按 PTP/IP → SSH → STA → Wi-Fi 所有权顺序释放；断连保持录像锁定且重连不重放命令；录像计时、转盘、媒体滑动均为局部更新。
- **测试与文档**：`tools/ui-render/sony_mock.{h,cpp}` 记录状态复制次数，`render.cpp` 新增空闲页不复制、单次修订只复制一次的断言。`export.py --sony-stage8` 导出 35 页同源截图到 `ui-preview/sony-camera/lvgl-stage8/`。新增 [阶段 8 验收报告](SONY_CAMERA_STAGE8.md)，README 增加 Wi-Fi 高级控制说明。

#### 构建与测试

| 检查 | 结果 |
|---|---|
| ESP-IDF 5.5.5 `./tools/build.ps1 -BuildDirectory build-toolbox` | 编译、链接、分区校验通过，构建日志中无警告 |
| `sony_protocol_check.exe`、`sony_connection_check.exe`、`camera_backend_check.exe` | 全部通过（桌面模拟） |
| `render.exe`、`render.exe full` | 全部通过（桌面模拟），包含新增状态复制断言及全部既有 Sony、原 BLE、表盘、抽屉、控制中心和五应用断言 |
| `python tools/ui-render/export.py --sony-stage8` | 35 页截图及 [总览](../ui-preview/sony-camera/lvgl-stage8/overview.png)，逐页检查圆屏边界、字形、热区与图标 |
| SSH 回环 | 未重跑，本轮未修改 SSH、隧道和会话代码 |

桌面构建需在导出 ESP-IDF 环境（提供 CMake/Ninja）后调用 MSVC `vcvars64.bat`。改动头文件后使用 `cmake --build build-ui-render --clean-first`。导出截图需在未导出 ESP-IDF 的 Python 中运行（ESP-IDF 虚拟环境没有 Pillow）。

#### 实际资源

- BIN 5,166,048 B → 5,167,008 B（+960 B）。factory 分区 12,386,304 B，剩余 7,219,296 B（58.3%）。
- `python -m esp_idf_size`：Flash 5,035,084 B（+972 B），静态 IRAM 16,384 B，共享 D/IRAM 静态占用 151,095 B（不变；`.data` 27,116 B、`.bss` 35,696 B）。各对象占用见[验收报告资源表](SONY_CAMERA_STAGE8.md#4-资源占用实测构建产物)。
- 没有新增任务、缓冲或缓存。正式 UI 下的板载 Heap、PSRAM、DMA 与栈余量仍待串口采样。

#### 刷写

- 经用户要求，通过 COM3（ESP32-S3 QFN56 v0.2）把 5,167,008 B 应用镜像写入 factory 分区 `0x20000`，esptool 哈希校验通过并硬复位。只写应用分区，保留 NVS、设置和用户存储。复位后串口在 6 s 内没有输出（启动日志在打开端口前已结束），开机状态由用户在设备上确认。
- 未提交、未推送。

#### 待实机验收与后续

- 按 [阶段 8 实机验收清单](SONY_CAMERA_STAGE8.md#6-a7m5-实机验收清单建议顺序) 第 1–10 项执行，结果回填本文件。重点：录像启停无音、录制中触摸与 KEYA/组合键锁定、参数读回、按键与拨轮 ID、媒体时间与删除范围、原 BLE 与系统回归、正式 UI 资源采样，以及新返回键与控制台布局的实际触控手感。
- 实机发现 ID 或枚举不一致时，只按实测修正映射表，不改协议框架。
