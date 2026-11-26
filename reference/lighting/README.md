# Multi-brand-light-link 1.0.20 · APK 互操作参考源码

从上游 [Multi-brand-light-link 1.0.20 Release](https://github.com/Dog-brother0113/Multi-brand-light-link/releases/tag/1.0.20) 获取 `Multi-brand-light-link-v1.0.20.apk`。解包后的参考材料保留在本地，用于协议核对和验证。ToolBox OS 固件构建无需这些材料。

- 上游 [1.0.20 MIT LICENSE](https://github.com/Dog-brother0113/Multi-brand-light-link/blob/1.0.20/LICENSE)；作者及版权归原作者，完整许可证随压缩包保留。
- APK 包含 Telink Android BLE Mesh、Spongy Castle、AndroidX Annotation 等第三方库；对应 NOTICE 与许可证一并保留在压缩包内，不统一改称 MIT。
- `sources/`：JADX 1.5.3 全量 Java 输出（包括原生应用与依赖）；`fallback-sources/`：为存在不完整反编译方法的 Java 类补充低层指令视图。
- `assets/`：APK 原始 JavaScript、HTML、CSS 与第三方许可文件；`resources/`：解码后的 Manifest 和 XML 资源。
- 不包含 APK 二进制、签名文件或与参考源码无关的两张 PNG（其中一张为收款二维码）。
- JADX 输出并非可直接重新构建的 Android 工程，部分 Java 方法恢复不完整，应结合 fallback 与原 APK 指令核对，不能据此认定所有协议均已经实机验证。

## 本地 Codex 使用

在项目根目录中将 APK 的 `assets` 解压到本地构建目录，或使用已有的本地参考归档：

```powershell
python -m zipfile -e .\Multi-brand-light-link-v1.0.20.apk .\build-lighting-reference
# 若已有本地参考归档，可改用以下命令。
Expand-Archive -Path .\reference\lighting\Multi-brand-light-link-1.0.20-decompiled.zip -DestinationPath .\build-lighting-reference -Force
```

Linux/macOS：

```sh
unzip -q Multi-brand-light-link-v1.0.20.apk 'assets/*' -d build-lighting-reference
```

参考路径：

- `build-lighting-reference/sources/com/pianchang/lighting/`：应用自己的 Java 桥接层。
- `build-lighting-reference/assets/`：各品牌通信协议及上层控制逻辑。
- `build-lighting-reference/fallback-sources/`：存在反编译缺口的对应 Java 类的低层指令形式。

生成协议参考测试向量：

```sh
node tools/lighting-test/generate-reference.cjs build-lighting-reference/assets
```

继续依据 [本地 Codex 任务书](../../docs/lighting/CODEX_HANDOFF.md) 与 [APK 分析记录](../../docs/lighting/APK_ANALYSIS.md) 开发。解压目录只供本地阅读，无需再次提交或纳入 ESP-IDF 编译。
