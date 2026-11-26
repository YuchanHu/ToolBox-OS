# 灯控固件构建与资源报告

## 本地集成版 · 2026-11-26

灯控集成时以 `main` 的 `b9a17eb` 为基线并保留 Sony 相机开发改动。灯控来源为 `origin/feature/multi-brand-lighting` 的 `60438e2`。下列数据记录该次集成构建及验证。

| 项目 | 结果 |
|---|---|
| 目标 | M5Stack StopWatch，ESP32-S3，16 MiB Flash |
| 工具链 | 本机 ESP-IDF 5.5.5，Xtensa GCC 14.2.0 |
| 完整源码配置、链接和映像 | 通过 |
| APK JavaScript 向量重生 | 53 组与仓库预期字节一致 |
| C++ 离线协议测试 | 53 通过、0 失败，含四个 Mesh 协议族的操作码检查 |
| 桌面 LVGL | 双横向刻度尺、灯具选择页四个完整选项、抽屉中文与图标、HSI 度数、异常广播名称通过验证。9 张圆屏预览通过裁切检查 |
| 其他软件回归 | Sony GATT、PTP/IP、配网，以及桌面系统、秒表和工具 App 用例通过 |
| 字体验证 | 灯控 20 px / 26 px MiSans 常用字形改为 4 bit，原字号回退字体覆盖罕见广播字符。P60c 预览中标题和状态的非黑色灰阶各由 3 级增至 15 级 |
| COM3 开发板 | 4 bit 字体版写入校验通过，串口确认从 `0x20000` 载入并进入 `app_main` |
| 实体灯具及真机运行时内存 | 待实机验收 |

[圆屏预览](../../output/lighting-preview/overview.png) · [完整验证记录](../../output/lighting-verification)。App 镜像和烧录参数可用下方构建命令生成。

### Flash 分区

分区表已从最终编译的 `partition-table.bin` 解码确认。

| 区域 | 偏移 | 大小 |
|---|---:|---:|
| Bootloader 镜像 | `0x000000` | 20,832 B |
| 分区表镜像 | `0x008000` | 3,072 B |
| NVS | `0x009000` | 16 KiB |
| PHY Init | `0x00F000` | 4 KiB |
| Factory App | `0x020000` | 12,386,304 B (`0xBD0000`) |
| FAT Storage | `0xBF0000` | 4 MiB |
| Core Dump | `0xFF0000` | 64 KiB |

最终 `ToolBoxOS.bin` 为 **5,632,224 B** (`0x55F0E0`)，Factory 余量 **6,754,080 B** (`0x670F20`)，约 **54.5%**。相较开发前保留的本地相机基线镜像 5,167,008 B，增加 465,216 B。

### 链接资源

| 项目 | 集成前本地基线 | 集成后 |
|---|---:|---:|
| Flash 链接段 | 5,035,084 B | 5,498,308 B |
| 专用静态 IRAM（含向量） | 16,384 B | 16,384 B |
| 共享 D/IRAM 静态占用 | 151,095 B | 165,859 B |
| 共享 D/IRAM 未静态占用 | 190,665 B | 175,901 B |
| 共享 D/IRAM `.data` | 27,116 B | 29,056 B |
| 共享 D/IRAM `.bss` | 35,696 B | 48,480 B |

共享 D/IRAM 静态增加 14,764 B。蓝牙和 Mesh 配置优先使用外部 RAM，连接、扫描和入网时的堆峰值需实测。剩余链接空间不等同于运行时可分配堆。构建包含 ESP-IDF Mesh 结构体初始化的编译告警，链接和镜像校验成功。详见 [链接汇总](../../output/lighting-verification/idf-size.txt) 和 [按库明细](../../output/lighting-verification/idf-size-archives.txt)。

### 复现

使用本机 IDF 5.5.5。旧工作区已有的 `sdkconfig` 中启用 `CONFIG_BLE_MESH=y`，新配置由 `sdkconfig.defaults` 启用。此轮使用独立构建目录和配置文件保留原构建产物。

```powershell
idf.py -B build-lighting-firmware -D SDKCONFIG=build-lighting-firmware/sdkconfig -D TOOLBOX_SONY_PROBE=OFF build
cmake --build build-ui-render --target render lighting_protocol_check
.\build-ui-render\lighting_protocol_check.exe
.\build-ui-render\render.exe lighting
python tools/ui-render/export.py --lighting
```

构建目录包含 Bootloader、分区表、App 和烧录参数。已有同版单 Factory 分区布局的 StopWatch 可只更新 `0x20000` 处的 App。详见 [构建与刷写指南](../BUILD.md)及[实体灯具验收清单](DEVICE_ACCEPTANCE.md)。

## 云端首版 · 历史记录

| 项目 | 结果 |
|---|---|
| 主项目基线 | b9a17ebe5142f8fba63cb95ce194c87f3a77edd8 |
| 固件源码本地构建提交 | bbeb08b292021100114ca66a01bf00ad27cb6a3b |
| 目标 | M5Stack StopWatch / ESP32-S3，16 MiB Flash |
| ESP-IDF | 5.5.5 |
| 完整构建 | PASS |
| 编码向量 | 44 passed, 0 failed |
| 应用镜像 | 5,245,280 bytes (0x500960)，约 5.00 MiB |
| Factory 分区 | 12,386,304 bytes (0xbd0000) |
| 分区余量 | 7,141,024 bytes (0x6cf6a0)，58% |
| 实体灯具测试 | 未执行 |
| 真机运行时 RAM / UI / BLE 稳定性 | 未验证 |

构建产物为 `ToolBoxOS.bin`，单 Factory 布局的 app 偏移为 0x20000。不要在未经确认分区布局的设备上直接刷写；不要擦除存量 NVS/文件卷，详见 [验收说明](../LIGHTING_PROTOCOL.md)。

正常本地构建由 `idf.py -B build-toolbox build` 复现；当前提交增加说明文档和复现脚本，未改变已编译的 C++ 固件源码。先完整交付，再集中实机验收。
