# Multi-brand-light-link APK 静态分析归档

分析样本：`Multi-brand-light-link-v1.0.20.apk`，上游 [Dog-brother0113/Multi-brand-light-link](https://github.com/Dog-brother0113/Multi-brand-light-link) 的公开 Release。APK 是互操作参考，不随本仓库再次分发。JS 资产通过 ZIP 解包获取，原生 classes.dex 通过 JADX 1.5.3 检查。相关原生类位于 `com.pianchang.lighting`；JADX 总计 1838 类有 2 处反编译错误，不能因此声称所有代码完整恢复。全量 Java 输出、存在不完整方法的类的 fallback 指令视图、原始 JS/HTML/CSS 和解码 XML 已归档：[reference/lighting](../../reference/lighting/README.md)。本地解压后使用其中的 assets 目录复现向量。

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
