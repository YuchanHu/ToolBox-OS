# 多品牌灯光控制 · 首版验收

本分支以 Multi-brand-light-link v1.0.20 Android APK 为互操作参考，对 APK 内的灯具通信模块及 Android GATT / Mesh 桥接进行静态分析，在 ToolBox OS / ESP-IDF 5.5.5 中重新实现前台灯控。未将 APK 或反编译代码加入仓库；尚未连接任何实体灯具。

## 实现边界

- 启动器新增「灯光控制」，最多保存 8 个灯位，单次控制一个选中灯位。
- 依据型号能力提供开关、亮度、CCT、HSI、G/M，以及部分有明确命令的灯效；未知能力不强制开放。
- 离开 App 停止灯控活动、断开连接、结束扫描／广播／Mesh。已下发的灯光状态由灯具维持。
- 不要求手机 App 常驻、云转发或网络连接。Mesh 入网使用本机新网络，不导入原厂 / Multi-brand-light-link 已有网络。

| 品牌 / 系列 | 已实现的传输 | APK 证据 / 验收重点 |
|---|---|---|
| 影器 INKEE | BLE 厂商广播 | 0x004e，16 字节负载；配对广播与控制广播；核对配对、亮度、色温及型号能力 |
| 永诺 Yongnuo | BLE GATT | F000AA60 / AA61 / AA63，多个协议变体；查询后核对 CH 频道 |
| 爱图仕 / amaran（Sidus） | BLE Mesh | SIG 0x26，厂商 0x0211 / 0x03f6；重新入网、AppKey 绑定、CCT / HSI |
| 轻装时代 LYD | BLE 厂商广播 | 频道与分组，需要与实体灯设置一致 |
| 神牛 Godox | BLE Mesh | Mesh 产品识别、入网与开关/CCT/HSI |
| 印迹 Lumin | BLE Mesh | Mesh 入网、型号及消息 |
| 金贝 Jinbei | BLE Mesh | 常亮灯模型、型号限定灯效；不开放闪光灯、不含 Pairlink 专有流程 |
| 新摄角 XSJ | BLE GATT | 握手、写入和通知解析 |
| 南光 / Nanlite | BLE GATT（USR） | 直接 GATT 型号；Feasy Mesh / Netrix 暂不支持 |
| Ulanzi | BLE GATT | 型号数据、型号限定灯效 |
| 智云 Zhiyun | BLE GATT | 读取控制地址后下发，不猜测地址 |
| 飞面 / 过曝 | 未实现 | APK 中未找到对应协议；需要品牌 APK 或协议材料 |

本机 Mesh 是 ESP-IDF Provisioner 的新网络，AppKey 绑定、重新入网和厂商 Mesh 的兼容性仍是验收重点。部分灯具必须先退出原网络或通过自身菜单重置蓝牙；重置前确认是否接受原有灯光网络配置被清除。

## 软件测试

```sh
c++ -std=c++17 -I main/system/lighting tools/lighting-test/test.cpp main/system/lighting/protocol.cpp main/system/lighting/products.cpp -o /tmp/lighting-tests
/tmp/lighting-tests
idf.py -B build-toolbox build
```

44 组由 APK 协议函数产生的固定报文向量与 C++ 实现对比，属于纯软件编码验证（不代表射频、连接、时序或实体灯已通过）。固件使用当前仓库 16 MiB、单 Factory 分区布局。

## 实机验收顺序

1. 先确认新固件启动、菜单/秒表/计数器等原有 App 正常；确认设置中的蓝牙开关正常，退出灯控后相机遥控可重新连接。
2. 影器：实体灯进入配对模式 → 新建灯位 → 配对 → 开灯 → 亮度从 10% 到 50% → 色温 3200 K、5600 K（限可变色温型号）→ 关灯。
3. 永诺：扫描并选择正确型号 → 读取或设置 CH → 同样测试亮度、CCT、开关；RGB 型号测试 HSI。
4. 爱图仕 / amaran：让待验收灯进入 Mesh 入网模式（已有网络可能需要明确重置）→ 选择灯具 → 等待入网、模型发现及 AppKey 绑定 → 同样测试开关及各支持模式。
5. 按上表逐一验收其他品牌；只对支持灯效的具体型号测试灯效。先确认单灯，再验证保存两个不同灯位和切换连接。
6. 检查退出与重新打开、重启设备后保存状态和连接、灯具不在附近时的错误显示。确保没有异常重启、卡死或误控制其他设备。

记录：品牌 / 型号 / 灯具固件版本 / 是否发现与连接 / 开关 / 亮度 / CCT / HSI / G-M / 灯效 / 重连 / 串口日志。出现异常时，只需将结果与日志一并反馈。

## 刷入前

仅适用于 ToolBox OS 当前单 Factory 分区固件（app 偏移 0x20000，分区大小 0xbd0000）。核对当前设备布局，备份重要数据。已有兼容布局的设备可只刷 app，不擦除 NVS／文件卷：

```sh
esptool --chip esp32s3 --port PORT write-flash 0x20000 ToolBoxOS.bin
```

旧布局（例如早期 1.1）、空白设备或无法确认分区时，不要盲目执行上述命令，参照 BUILD.md 的迁移/完整烧录说明。串口日志可用 115200 波特率采集。
