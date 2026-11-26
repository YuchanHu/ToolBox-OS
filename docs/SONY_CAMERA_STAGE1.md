# Sony α7 V 协议检查与验收方案

2026-11-25 · ToolBox OS 1.2.0 · 代码基线 `b9a17ebe5142f8fba63cb95ce194c87f3a77edd8`

本阶段完成项目检查、源码核对和可脱离相机的验证。A7M5（ILCE-7M5）实机验收待执行。按[任务书](SONY_CAMERA_TASK.md)和 [V4 原型](../ui-preview/sony-camera/sony_camera_ui_v4.html)执行，构建和测试结果见[进度记录](SONY_CAMERA_PROGRESS.md)。

## 项目与可复用实现

| 范围 | 当前实现及衔接 |
|---|---|
| 应用和前台生命周期 | `main.cpp` 初始化 HAL 和 Mooncake，`system/app_registry.cpp` 注册 `AppCamera`，`system/shell.cpp` 管理抽屉、表盘和浮层。`app_camera.cpp` 通过 `onOpen/onRunning/onClose` 开启、更新和释放 BLE 页面与会话。新入口可复用该生命周期交接。 |
| BLE | `system/ble.cpp` 管理 Bluedroid 开关及资源，`system/camera_remote.cpp` 管理 Sony GATT 状态，`system/sony_protocol.h` 保存 RMT-P1BT 按键及通知，`system/camera_ui.cpp` 渲染现有页面。复用原应用入口和协议，保留现有 BLE 行为。 |
| Wi-Fi 与配置 | `hal/hal_network.cpp` 通过统一控制器协调 `esp-wifi-connect` 的 STA、掉线、配置 AP 和恢复，另有工牌 AP 交接。已有 `assets/provision.html` 与 HTTP 配网实现可供手机辅助复用。后续需让相机 STA 走同一网络所有者，单独维护相机配置并保存、恢复原策略，避免系统回退到配网 AP、影响天气或覆盖家庭网络凭据。 |
| NVS | `hal/utils/settings` 提供布尔、字符串和 blob 持久化。当前 `sdkconfig` 未启用 NVS 加密或 Flash 加密。现有机制可存配置及已确认的指纹，不能把当前构建描述为具备静态凭据加密。临时配网 AP 的访问方式和相机凭据保存方案留待配置阶段落实。 |
| 按键、反馈、触摸 | `hal_button.cpp` 的 `updateButtonStates()` 可立即播放公共按键反馈，`applicationButtonFeedback` 支持应用接管，`buttonFeedback(keyB, false)` 支持仅振动。音量走 `runtime.cpp` → `hal_audio.cpp`，振动已有任务。现有 BLE 页面只在 Movie 屏接管反馈，且先判断 `GoHome` 再读取录像状态。高级录像应在公共反馈和全局组合键之前处理拦截，并复用 `system/touch_input` 的锁。 |
| 显示和解码 | `hal_display.cpp` 在 468×466 面板传输范围使用 RGB565_SWAPPED、PSRAM 双帧缓冲、内部 DMA 条带、GPIO38 TE 与独立 4 ms 触摸采样。圆屏 UI 布局以 466×466 为基准，沿用 MiSans 和现有 LVGL 绘制。优先评估 M5GFX 的 `lgfx_tjpgd` baseline JPEG 解码到 PSRAM 媒体缓冲。现有配置的解码回调输出 RGB888，需按块转换成 RGB565 并明确字节序。渐进 JPEG 等失败时按真实预览能力处理。 |

已阅读 V4 的页面、`action`、`key` 和触摸处理。`?overview` 进入 21 页总览，常规模式可走配网、BLE 交接和高级控制演示。后续保留九宫格多击与明文输入、录像按键及触摸锁、参数确认、转盘 15° 初始步进、最新媒体及固定回看时间基准。原型已有模拟属性、指纹、媒体和录像状态。录像原型在秒变化时重绘整个视图，拨轮页原型 KEYB 会产生动作，正式实现以任务书的局部刷新及无额外 KEYB 动作为准。

## 固定参考版本与许可证

| 参考 | 查阅内容和用途 |
|---|---|
| [alpha2go `02883425`](https://github.com/Knallli/alpha2go/tree/02883425c9445c653b2950210187cdd1547320c2)，Apache-2.0 | `ptpip/ssh.go` 的 `NewSSHDialer`，`client.go` 的双通道与事务，`ops.go` 的 `SonyHandshake`，`sonyprops.go`/`sonyinfo.go` 的 `SonyGetProps`、`SonySetProp`，`sonycontrols.go` 的按键编码，`sonyremote.go`/`sonyadmin.go` 的 `SonyListAllContents`、`SonyGetContentsCompressed`、`SonyDeleteContent`。核对 `ptpip/ptpiptest`、`*_test.go`、`cmd/ptpprobe` 和 `docs/protocol.md`。α6700 固件 2.00 为上游真机证据。后续仅按需独立编写 C/C++；若移植受许可代码，保留 Apache 许可、归属及 `NOTICE`，标示修改。 |
| [FX30-Controller `85604cbe`](https://github.com/thiagolegrand/FX30-Controller/tree/85604cbe3930da077616b8cb8f818dbfb45c73fd)，MIT | `src/net/WiFiManager.h`、`src/main.cpp` 和 `src/storage/Store.cpp` 的 STA、扫描、配置与掉线流程，`src/core/Transport.h`、`src/protocol/{ptpip,ptp,sony}` 的传输与 UI 分工、双通道、SDIO、属性、事件和控制。核对 `test/native`、FX30 v300 属性目录和原始十六进制样本。FX30 固件 6.00 的明文方案使用关闭访问验证的相机设置。A7M5 方案保持认证开启，沿用概念和测试结构。`SonyCamera::recStart/recStop` 在命令成功后更新本地 `_recording`，该值不能当实机反馈。 |
| [Alpha-Fairy `fd1f2989`](https://github.com/frank26080115/alpha-fairy/tree/fd1f2989f952a77d01cdb23b788058c39f091c89)，MIT | `arduino_workspace/AlphaFairy` 的 `WifiUtils.ino`、`WifiHandlers.ino`、`HttpServer.ino`（表单提交）、`Intervalometer.ino`（调度与中止）、`FocusStack.ino`（MF、步进、拍摄和恢复）。参考配置及取消流程和镜头相关限制。后续以系统已有 ESP-IDF 服务和相机报告能力为准。若复用 MIT 代码保留许可证与归属。 |
| [playmiel/libssh2_esp32 `26814617`](https://github.com/playmiel/libssh2_esp32/tree/26814617eb1f880c6f998e2744f15918947ec577)，BSD-3-Clause | 唯一 ESP-IDF SSH 客户端候选。原生组件依赖 `mbedtls`、`esp_netif`，mbedTLS 后端有 3.x 及 3.6 兼容分支。现有 IDF 5.5.5 带 mbedTLS 3.6.6。用同一 session 的两个 `libssh2_channel_direct_tcpip_ex`，认证使用 `libssh2_userauth_keyboard_interactive_ex`，握手后先用 `libssh2_hostkey_hash` 的 SHA-256 与已确认指纹核对。签名/密钥交换重点检查 ECDSA NIST P-256、ECDH、RSA-SHA2，密文和 MAC 检查 AES-CTR/HMAC-SHA2。该 mbedTLS 后端关闭 Ed25519，不能预判相机协商结果。仅供 API 和后端交叉参照的[上游 libssh2 `dd154670`](https://github.com/libssh2/libssh2/tree/dd1546700d0ed8ff7138bc1ccc288286fb3ab2b4)不会作为第二套客户端引入。 |
| [ewpa/LibSSH-ESP32 `343fce1c`](https://github.com/ewpa/LibSSH-ESP32/tree/343fce1c5d1d0e3d629807dece6ae1b68ef1d322)，`COPYING` 为 LGPL-2.1 | `auth.c` 有 keyboard-interactive，`channels.c` 有 `ssh_channel_open_forward`，具备会话、多通道、公钥摘要 API 与 mbedTLS 配置。组件构建直接依赖 Arduino，需额外验证密码套件和链接分发义务，且会引入额外运行时。现阶段选择原生 IDF 的 libssh2 候选。 |
| [esp_new_jpeg 1.0.2 `7162d323`](https://github.com/espressif/esp-adf-libs/tree/7162d32381d6e1f8c8cba2ee5edcbfe82bc964b1/esp_new_jpeg)，Espressif MIT | `include/esp_jpeg_dec.h` 与 CMake 定义提供 ESP32-S3 的预编译库和 RGB565 解码选项。许可证授权在 Espressif 产品上使用，分发需保留许可。仅在现有 JPEG 解码器经实拍样本测量确有缺口时评估，实际 Flash、工作区与速度待测。 |
| Sony 官方 | [机身遥控帮助](https://helpguide.sony.net/ilc/2540/v1/zh-cn/contents/0903_pc_remote_function.html)、[访问身份验证](https://helpguide.sony.net/ilc/2540/v1/en/contents/221h_access_authentication_settings.html)、[Wi-Fi 频段](https://helpguide.sony.net/ilc/2540/v1/en/contents/0706_wifi_frequency_band.html)、[Wi-Fi Direct 模式](https://helpguide.sony.net/ilc/2540/v1/en/contents/241h_wifi_direct_settings.html)是 A7M5 官方能力证据。[Camera Remote SDK](https://support.d-imaging.sony.co.jp/app/sdk/en/) 2.01.00 起加入 ILCE-7M5，公开页列 2.02.00。逐型号接口和功能以随包 API Reference 为准。[Camera Remote Command](https://support.d-imaging.sony.co.jp/app/cameraremotecommand/en/index.html)列 ILCE-7M5，命令参考按地区申请。本轮核对公开资料，SDK 包和受限命令参考留给授权桌面验证。 |

本阶段只记录协议与代码证据。参考源代码和样本在临时目录，未纳入固件或仓库。SSH 候选的 IDF 链接、实际协商和运行资源在最小通信阶段测量。

## A7M5 协议能力矩阵

**官方确认**表示 Sony 文档明确描述 A7M5 功能。**待实机**表示线协议或条件需要本项目复测，α6700 与 FX30 的证据只适用于其各自机身及固件。**不支持**在有确证时记录具体机身、模式与响应，此阶段已知限制列在表内。

| 能力 | 证据、数据及边界 | A7M5 状态 |
|---|---|---|
| 2.4 GHz Wi-Fi Direct、访问身份验证 | Sony 帮助确认相机作为 AP，显示 SSID/密码及独立的用户名、密码、指纹。支持多频段的机身可选择 2.4 GHz。Windows 直连和 ESP32-S3 STA 获取地址、重连仍需验证。 | 官方确认，待实机 |
| SSH | α6700 在 TCP 22 上使用 SHA-256 主机指纹及 keyboard-interactive。验证主机密钥成功之后才发送密码。同一 SSH 会话打开命令和事件两个 `direct-tcpip`，目标字面量 `localhost:15740`；α6700 拒绝 `127.0.0.1:15740`。 | 端口、目标、认证方式、协商套件和双通道待实机 |
| PTP/IP 与 Sony SDIO | `InitCommandRequest/Ack` 获得连接号，`InitEventRequest/Ack` 使用同一号。`GetDeviceInfo`、`OpenSession` 后，alpha2go 发 `0x9201(1,0,0)`、`0x9201(2,0,0)`、`0x9202(0x12C,1)`、`0x9201(3,0,0)`；记录实际返回版本及属性/控制清单。FX30 有自己的握手变体。 | 型号、协商版本、清单及实际事件待实机 |
| 属性、设置及机身同步 | PTP3 `0x9209(0)` 全量，`0x9209(1)` 增量，`0x9251(code)` 单项，`0x9205(code)` 的数据阶段写入按描述符编码的值。`0x9209` 有 LE `u64` 条目数，条目含 `u16` 属性码、`u16` 类型、`u8` 可写及启用、默认/当前和 range 或 settable/reportable enum。参考事件 `0xC203` 触发增量读取。 | 照片/录像各自实际参数、只读/可写、允许值、依赖项、改参读回及事件待实机 |
| 拍照 | `0x9207` 的 `0xD2C1` 半按、`0xD2C2` 全按，数据 LE `u16`，`2` 按下、`1` 释放。α6700 的 AF/AE、无 AF 条件和延时仅作参考。以机身写卡或经验证的完成证据计数。 | 快门、拒绝及完成依据待实机 |
| 录像启停、状态与当前时长 | Alpha2go 用 `0x9207(0xD2C8)` 的 LE `u16` 按下、释放切换。FX30 的 start/stop 分别发送值 2/1，操作语义需按机型和实测确认。[libgphoto2 v2.5.32](https://github.com/gphoto/libgphoto2/blob/v2.5.32/camlibs/ptp2/ptp.h)列 `0xD21D` 为 Sony 录像状态。两个上游 `0x9209` 样本均含只读 `u8` 的 `0xD21D`，样本值都是 0，范围分别为 α6700 的 0–3 和 FX30 的 0–2。A7M5 的枚举含义依实机描述符及实际变化判定。暂未取得本段实时时长或开始时间的 A7M5 证据。[Sony TC/UB](https://helpguide.sony.net/ilc/2540/v1/en/contents/0601D_TC_UB.html)说明 Rec Run 也可继承上一段计数，Free Run 空闲照走，单次读数不能当本段时长。 | 照片模式 MOVIE、机身与远程启停、恢复同步及可信计时待实机 |
| `0xD208` / `0xD20A` | 各是按键和拨轮的可用 ID 描述符，以 settable enum 取 ID，当前值无需写入。本轮解码的 α6700 样本和 FX30 v300 样本均为 `u16`，α6700 协议笔记中的 `u32` 与其样本不符。结合会话清单、描述符类型及启用字段按机型解析。 | 支持项、位宽和条件待实机 |
| `0xD309` 机身按键 | `0x9207(0xD309)` 数据 LE `u32`，`buttonID<<16` 加低位 `2` 按下、`1` 释放。α6700 上游要求 `0xD20C` 为 `1`、一次一个按键。两份样本的 `0xD20C` 均是 `u16`（α6700 笔记标 `u8`）。方向/SET/MENU/Fn/回放/AF-ON/C1–C3 等仅验证机身公布的映射。 | 每个按键、忙碌响应、释放待实机 |
| `0xD30B` 控制转盘、前后拨轮 | `0x9207(0xD30B)` 数据 LE `i32`，高 16 位拨轮 ID，低 16 位 `int16` 步数（正值顺时针）；α6700 用 `0x4001/0x4002/0x4003`，FX30 属性样本也列这三个 ID。动作受当前曝光模式、拨轮分配和机身状态影响。 | 三轮正/反步进和模式依赖待实机 |
| `0x923B` 拍摄日期列表 | 命令参数 `[slot]`（PTP 参数为 LE `u32`）。数据 `u32` 数量加对应的 `u64` 毫秒值（参考 α6700，机身墙钟编码为 UTC 形式）。先测此操作识别空卡和可访问性。参考实现只在此处把 `InvalidParameter/StoreNotAvailable` 视为空或缺卡候选。 | 卡槽、日期语义、空卡及拒绝待实机 |
| `0x923C` 内容与文件 | `[cursorLo,cursorHi,100,slot,0]`，初始游标 0，参考实现以末项 CreatedUTC 毫秒推进到下一页。返回头长 `0x90`，后接 `u32` 条数和变长记录；Content ID `u32`，含组、保护及 UTC/本地创建与修改四个 `u64` 时间；file ID `u16` 占四字节槽，另有路径、格式、尺寸、UMID。相同时间游标防停滞，空日期列表跳过分页，状态限制和超时单独记录。最近项需核对时间语义后在可访问卡槽间选择。 | 分页、双卡、RAW+JPEG/RAW+HEIF 成组、真实文件名和拍摄时间待实机 |
| `0x923E` 缩略图 / Screennail | `[contentID, (slot<<24)\|fileID, kind]`，`kind=1` 缩略图、`2` Screennail，返回图像字节。按文件探测真实格式和尺寸。α6700 可返回 ARW 的 JPEG 缩略图，也可返回 HIF 的 HEIF 缩略图/预览。项目只解码静态 JPEG，若该资源只提供 HEIF 或协议拒绝，则展示占位。 | JPEG/RAW/RAW+JPEG/HEIF/视频分别待实机 |
| 已录视频时长及拍摄开始时间 | `0x923C` 视频可选块有 `19 × u32`，alpha2go 解析器只标记存在，未解码。核对时间字段、已录时长与机身回放或 Sony API；Created 或 Modified 在语义确认前不直接当视频起始时间。 | 未知，待实机。无法取得的时长留空 |
| `0x9250` 删除与变更事件 | 参数 `[contentID,slot]`，无数据输出阶段。删除单位是内容，可能包含多个文件。α6700 上游观察到 `0xC240(slot,contentID,1)`，以同卡重列确认目标消失，并核对其他内容及配对范围。 | 需单独取得指定测试内容的用户确认后实测 |
| 自动化 | 间隔拍摄可基于已确认的快门与实际计数，预设可基于机身描述符及用户确认写入。Alpha-Fairy 的 MF 步进依赖镜头/状态。 | 基础拍照、参数及镜头步进待验证，焦点堆栈按支持情况开放 |
| 不支持及范围外 | 已知 α6700 单卡机身不支持 slot 2，SSH 不支持转发到 `127.0.0.1`。A7M5 暂无本项目的实测拒绝证据，不能给媒体/键/属性下不支持结论。正式应用排除实时取景、视频流/播放、原文件下载、RAW/HEIF 原图解码、格式化和固件修改。 | 前两项为 α6700 限制，项目范围按任务书 |

参考输入与输出字节顺序见固定版本的 [alpha2go 协议笔记](https://github.com/Knallli/alpha2go/blob/02883425c9445c653b2950210187cdd1547320c2/docs/protocol.md)和 [FX30 v300 属性目录](https://github.com/thiagolegrand/FX30-Controller/blob/85604cbe3930da077616b8cb8f818dbfb45c73fd/docs/fx30_props_v300_catalog.md)。属性位宽另对照两份上游原始抓样 [α6700 `0x9209`](https://github.com/Knallli/alpha2go/blob/02883425c9445c653b2950210187cdd1547320c2/ptpip/testdata/a6700-9209.bin) 和 [FX30 v300 `0x9209`](https://github.com/thiagolegrand/FX30-Controller/blob/85604cbe3930da077616b8cb8f818dbfb45c73fd/docs/fx30_props_0x9209_v300.hex)。

## Windows 桌面验证方案

1. 在独立目录检出上表固定的 alpha2go 提交。按 `go.mod` 使用 Go 1.26，运行 `go test -count=1 ./...` 和 `go build -o ptpprobe.exe ./cmd/ptpprobe`。Windows 摄影控制路径可用 Sony Imaging Edge Desktop Remote 作对照；需要更细的机型/API 限制时从 Sony 正常获取官方 SDK/Command 参考，并记录版本。每次只保留一个连接相机的客户端。
2. 在机身启用遥控拍摄，保持访问身份验证。从「Wi-Fi Direct 信息」取得 SSID/密码，从「访问身份验证信息」取得另一套用户名、密码和指纹。支持选择频段时选 2.4 GHz，先使用标准 Type1。断开手机，Windows 加入相机 AP，记录实际 DHCP 地址和网关、频道、信号及频段。通过该接口确认相机地址，可用 `ipconfig`、`netsh wlan show interfaces`、`Test-NetConnection <相机IP> -Port 22` 辅助排查。无互联网属于测试拓扑预期。
3. 先做只读探测。下列 PowerShell 7 脚本在运行期收集输入，子进程只用环境变量接收 SSH 密码，结束后清理。先对照机身菜单确认指纹，再启动探测。`-list 0` 跳过探针默认的标准对象列表，维持遥控模式。

   ```powershell
   $cameraAddress = Read-Host '相机 IP'
   $cameraUser = Read-Host '机身显示的访问身份验证用户名'
   $cameraFingerprint = Read-Host '已对照机身确认的 SHA256 指纹'
   $probeArgs = @('-host', $cameraAddress, '-ssh-user', $cameraUser,
       '-ssh-fingerprint', $cameraFingerprint, '-sony=on', '-list', '0', '-timeout', '10s')
   $env:PTPPROBE_SSH_PASSWORD = Read-Host '机身显示的访问身份验证密码' -MaskInput
   try {
       & ./ptpprobe.exe @probeArgs -props -events 20s
       if ($LASTEXITCODE -ne 0) { throw "ptpprobe exited with $LASTEXITCODE" }
   } finally {
       Remove-Item Env:PTPPROBE_SSH_PASSWORD -ErrorAction SilentlyContinue
   }
   ```

4. 首轮通过后，按下表每次只启用一类操作。探针可用 `-getprop`、`-set`、`-capture`、`-control`、`-compressed`；再次运行沿用同样的短时凭据输入。需要同会话连续观测（录像状态、按键释放、双卡分页和事件归因）时，复用 `ptpip.Client` 的命名方法及 `Events()`，在桌面测试目录单独记录。`-control` 仅发送序列；用命名按键/录像函数做受控释放与超时测试。`-compressed` 会按指定 content 列表取缩略图和 Screennail，可用直接方法只取单文件单规格，以免预览失败掩盖已成功的结果。删除前必须核对最新列表与范围并取得用户确认。探针 `-delete` 可直接发命令，不提供交互式二次确认，本轮未调用。媒体验证使用已知命名方法，避免猜测 `0x923C` 参数、进入锁机身 UI 的 `0x9212` 模式或读取原文件。
5. 每条记录包含机型、固件、镜头、卡槽/卡和相机模式，记录时间/时区、工具版本、认证/握手结果、协商算法、操作码、数据类型、允许值、响应、事件及相机可见结果。分别标记通过、受模式限制、明确不支持、超时或待测，日志去掉凭据、完整指纹、序列号、MAC/UMID 和个人媒体路径。必要时保留脱敏的消息片段和用户同意的测试缩略图。

## A7M5 实机验收清单

以下项目截至本阶段均待执行。同一会话的成功判据以机身、协议响应及事件对应为准。

| 顺序 | 操作与验收证据 |
|---|---|
| 1. 网络与认证 | 无路由器 2.4 GHz 直连，确认相机地址和 SSH 认证方式。比较机身指纹；正常指纹通过，故意填错期望指纹一次应在发送密码前中止；连接已保存指纹时正常复核。记录 KEX、主机密钥算法、cipher/MAC、转发目标、两个 channel 的打开/关闭与重连。 |
| 2. 双通道及能力 | InitCommand/Ack 和 InitEvent/Ack 使用同一连接号，确认型号 `ILCE-7M5`、固件、PTP 扩展、SDIO 阶段、版本及实际能力表。相机物理改一个有效参数验证事件确实出自独立通道。断连、再连、关闭 session 时记录通道状态。 |
| 3. 参数 | 照片与视频模式分别读快门、光圈、ISO、EV、白平衡、对焦模式/区、防抖、电量及对应专用参数（照片驱动/RAW/JPEG/HEIF，录像格式、帧率、录制设置、码率、色彩采样与位深）。各选一个可恢复的允许值，经明确操作写入并读回，记录拒绝或只读情形、机身改参通知、重连读回。恢复测试前值需要明确操作。 |
| 4. 拍照 | 在测试卡上实拍，观察半按/全按/释放、机身忙碌或禁止条件、实际写卡与完成事件/重列，区分命令 OK 与完成。 |
| 5. 录像 | 照片模式和视频模式分别发启停，确认真实 REC 及媒体。对照物理 MOVIE、拒绝、录制中掉线和重连，读取真实状态并核查 `0xD21D` 等候选字段及事件。记录若干已知时长的短片，在 Rec Run 与 Free Run 对照本段真实时间。若没有可信计时字段，交接为相机确认后从远程指令发送的单调时间 +500 ms 估算，机身发起需标记估算限制。 |
| 6. 机身按键和拨轮 | 用 `0xD208/D20A` 逐项确认菜单、Fn、回放、四方向、SET、AF-ON、C1/C2/C3 及控制转盘/前后拨轮。核对真实动作、触发时的 `0xD20C`、按下/释放顺序、正负方向、忙碌拒绝和取消后释放。 |
| 7. 内容与时间 | 测试单卡、空卡及可用时双卡，读取列表和分页，记录 content、file、slot、文件名及组。拍摄一组有已知顺序和时间的照片与跨刻度短片，与回放时间比较，区分创建、写入、结束及真实开始，记录 UTC/机身时区/偏差，不校时。确认最新媒体及视频已录时长能否可靠获取。 |
| 8. 静态预览 | 对 JPEG、RAW、RAW+JPEG、HEIF、视频尝试实际可访问的 Thumbnail/Screennail，记录格式、尺寸、预览归属、视频时长字段来源及不支持或繁忙响应。 |
| 9. 指定内容删除 | 仅使用获用户确认的无价值测试内容。删除前核对 `(slot, contentID)`、file ID 集合、类型、文件名和整个配对范围，先测取消。授权后试一次目标删除，核对响应、`0xC240` 或其它变更事件、重列和机身回放，并验证临近文件仍在。 |
| 10. 后续 StopWatch 门槛 | 完成相机桌面链路后，在授权的设备测试环境验证 STA 与会话生命周期、照片/录像、强制静音及 KEYA/触摸锁、BLE/原 Wi-Fi 回归。采样空闲、STA、SSH KEX、双通道就绪、媒体解码和关闭后的内部 free/min/largest、PSRAM free/min、内部 DMA 余量与任务栈高水位。 |
