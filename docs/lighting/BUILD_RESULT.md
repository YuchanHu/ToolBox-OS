# 首版可刷机验收固件 · 构建记录

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
