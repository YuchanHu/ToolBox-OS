# Multi-brand-light-link APK 静态分析归档

分析样本：`Multi-brand-light-link-v1.0.20.apk`，上游 [Dog-brother0113/Multi-brand-light-link](https://github.com/Dog-brother0113/Multi-brand-light-link) 的公开 Release。JS 资产通过 ZIP 解包获取，原生 classes.dex 通过 JADX 1.5.3 检查。相关原生类位于 `com.pianchang.lighting`；JADX 总计 1838 类有 2 处反编译错误。使用本地 `assets` 目录复现向量，详见[参考材料获取方式](../../reference/lighting/README.md)。

## 证据索引

| 协议 | APK 文件/桥接线索 | C++ 实现 |
|---|---|---|
| 影器 | `inkee.js`；`LightingBluetoothBridge`；公司 ID 0x004e 的 16 字节厂商广播，配对 500ms、普通控制 250ms | `inkee_frame`，广播队列 |
| 轻装时代 | `lyd.js`，频道/分组广播 | `lyd_frame` |
| 永诺 | `yongnuo-protocol.js`、`android-gatt.js`、`LampGattTransport`，F000AA60/AA61/AA63 服务/写/通知，多个协议变体，CH 查询/控制 | `yn_*`、radio GATT |
| 爱图仕/amaran | `sidus-protocol.js`、`fixture-mesh.js`、`mesh-protocol.js`、`SidusAdvertisement`、`FixtureMeshTransport`：SIG 0x26，厂商 0x0211/0x03f6 | `sidus_control`、mesh Provisioner |
| 神牛 | `godox-protocol.js`、`fixture-mesh.js`、`FixtureMeshTransport`，常亮灯厂商模型 | `godox_control` |
| 印迹 | `lumin-protocol.js`、`LuminMeshProtocol` | `lumin_control` |
| 金贝 | `jinbei-protocol.js`、`JinbeiMeshProtocol`，型号限定灯效；不包含闪光灯控制 | `jinbei_control` |
| 新摄角 | `android-gatt.js` 的 xsjProtocol 和原生 GATT 桥接 | `xsj_join`、`xsj_control` |
| 南光 | `android-gatt.js` 的 usrProtocol；另有 `nanlink-mesh.js` | `nanlite_*`，仅 USR 直接 GATT |
| Ulanzi | `ulanzi-protocol.js`、`UlanziAdvertisement` | `ulanzi_control`、`ulanzi_query` |
| 智云 | `zhiyun-protocol.js`、`ZhiyunAdvertisement`，查询控制地址后使用 | `zhiyun_*` |

原生桥接还涉及 `MainActivity`、`LightingBluetoothBridge`、`LampGattTransport`、`FixtureMeshTransport`。产品识别、能力和部分灯效映射已整理为 `main/system/lighting/products.cpp`，仅将通信所需字段及型号事实转写为 C++，不照搬 Android 界面。

## 参考向量复现

用 ZIP 工具解包 APK，并定位包含上述 JS 文件的资产目录。执行：

```sh
node tools/lighting-test/generate-reference.cjs PATH_TO_EXTRACTED_JS_ASSETS > vectors.json
```

脚本仅在 Node VM 中调用 APK 的 JS 编码函数，模拟最小 Android 桥对象，不启动 Android 或蓝牙；输出是用于人工核对 `tools/lighting-test/vectors.inc` 的 44 组参考向量。该脚本针对 v1.0.20，若上游修改内部导出/函数名，需根据静态分析调整。它不自动覆盖现有测试预期。测试使用固定状态、地址和序号，不代表真实配对会话。

## 当前缺口

- BLE Mesh 采用 ESP-IDF 新建本机网络，不导入原厂 App 或参考 APK 的网络。参考 APK 部分品牌的 IV index 不同；实际 Provisioning、AppKey 绑定、模型地址与单网络方案兼容性必须在后续实机验证。
- 南光 Feasy Mesh / Netrix 暂未实现；金贝 Pairlink 流程未实现；飞面/过曝未找到对应协议。不得将缺失协议标为已支持。
- 目前仅对报文编码进行离线比对。实际硬件没有试灯、抓包、配对、延迟测试或测量可用 RAM；真实灯具固件/机型存在差异。
- 仅对用户自己持有或有权控制的灯具进行连接／验收，Mesh 重置前应确认原灯光网络配置允许清除。

完整品牌范围和验收顺序见 [LIGHTING_PROTOCOL.md](../LIGHTING_PROTOCOL.md)。

## 本地静态复核

本轮从本地 1.0.20 APK 提取 JavaScript，重生原有 44 组向量，另增加 9 组，覆盖影器两种参数的边界、永诺 CH 查询和 YN150ULTRABI 的 0.1% / 50 K 编码、Sidus 可变和固定色温、轻装时代色温进位。C++ 运行结果见 [逐条测试记录](../../output/lighting-verification/protocol-tests.txt)。

| 协议 | APK 静态依据 | 本轮核对结果 |
|---|---|---|
| 影器 | `inkee.js` 的 `encode`、`allocateAddress`，`LightingBluetoothBridge.java` 的 `startBroadcast` | 厂商 0x004e，16 字节，配对 500 ms、控制 250 ms。灯位使用本机控制器 ID、场景 1、独立组号。广播设为可连接，和 APK 的 `setConnectable(true)` 一致。Android 只指定低延迟档，控制器实际间隔和灯具接收成功率待实测。 |
| 永诺 | `yongnuo-protocol.js` 的 `products`、`identifyVariant`、`readStatus`，`LampGattTransport.java` 的 `selectCharacteristics`、`subscribe` | F000AA60/61/63。区分现代、小数和旧协议。产品代码优先识别型号能力。YN150ULTRABI 为 2700–6500 K、0.1%，YN360 MINI 为 2500–7500 K，经典 YN360 使用固定 CH 1。通知缺少 CH 时可手填可变频道型号。 |
| 爱图仕 / amaran | `sidus-protocol.js` 的 `settings`、`sidus0x26Commands`，`SidusAdvertisement.java`、`FixtureMeshTransport.java` 的 `SidusControl` | 两类厂商控制模型 0x0211/0x0000 与 0x03f6/0x1000 采用 SIG 0x26。可调色温的编码单位为 10 K，UI 沿用 50 K。固定色温使用亮度命令。当前 ESP-IDF 配网使用本机 Mesh 网络，APK 配网回读及原有网络导入仍有差异。 |
| 轻装时代 | `lyd.js` 的 `temperature`、`encode` | 厂商 0xffff，250 ms 广播，按 CH/分组定址，CCT 以 100 K 进位。 |
| 神牛 | `godox-protocol.js` 的 `parameterMessage`，`FixtureMeshTransport.java` 的 `Profile.GODOX` | 0x0211 厂商模型与 CRC8，CCT 以 100 K 编码。APK 神牛网络初始 IV index 为 1，本机统一 Mesh 网络从 0 开始，列为实机兼容性待办。 |
| 印迹 | `lumin-protocol.js` 的 `commands`，`LuminMeshProtocol.java` 的 `select` | 0xae86/0x0001 厂商模型，CCT 两字节。APK 另有产品状态回读，目前使用已识别型号的能力表。 |
| 金贝 | `jinbei-protocol.js` 的 `commands`，`JinbeiMeshProtocol.java` 的 `select`、`needsReadback` | 已接入 0x005d SIG Mesh 常亮灯厂商模型，CCT 以 100 K 编码。APK 会识别旧 Pairlink 广播，并区分候选模型和回读行为。专有 Pairlink 入网在本工程仍缺失。 |
| 新摄角 | `android-gatt.js` 的 `xsjProtocol`，`LampGattTransport.java` 的 `Profile.XSJ`、`onMtuChanged` | AE30 或 AE00，AE01 写、AE02 可选通知。优先使用 AE30，协商目标 MTU 128，最小有效 MTU 27。 |
| 南光 | `android-gatt.js` 的 `usrProtocol`，`LampGattTransport.java` 的 `Profile.NANLINK`，另见 `nanlink-mesh.js` | 直接 USR GATT 用 0003CDD0/D2/D1，优先带响应写入。Feasy 有独立 Mesh 配网、AppKey、Proxy 和厂商负载流程。Netrix 在原生桥接层单独识别，均为当前实现缺口。 |
| Ulanzi | `UlanziAdvertisement.java`，`LampGattTransport.java` 的 `prepareNotifications`，`ulanzi-protocol.js` 的 `readStatus` | 新增 F000FFC0/FFC1 握手，写 0x09 成功后订阅 FFF1，再经 FFF2 查询。状态回报更新 CCT 范围与 G/M 能力。 |
| 智云 | `zhiyun-protocol.js` 的 `capabilities`、`settings`，`android-gatt.js` 的目标地址选择 | FEE9 GATT。先查控制地址，PL105 有 APK 明示的 0x8003 回退值，PL103 等待有效查询结果。CCT 步进 100 K。 |

GATT 分发按已注册的客户端接口处理。灯控退出时清除待发送状态和写入队列，停止扫描、关闭连接或停播。Mesh 路径释放模型与扫描资源并保留配网记录。设备间切换和退出后相机重连的射频结果待集中实机验收。
