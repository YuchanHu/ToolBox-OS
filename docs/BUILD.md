# 构建指南

## ESP32-S3 固件

1. 安装 ESP-IDF 5.5.5 及该版本的 Python、Xtensa、CMake 和 Ninja 工具。
2. 在 ESP-IDF 终端中打开项目。Windows 可执行该安装目录下的 `export.ps1`，Linux/macOS 可执行 `. "$IDF_PATH/export.sh"`。
3. 使用现有 `components/`。需要取得缺失的固定版本依赖时运行 `python fetch_repos.py`。ESP-IDF Component Manager 按 `dependencies.lock` 读取托管组件。
4. 执行 `idf.py -B build-toolbox build`。产物为 `build-toolbox/ToolBoxOS.bin` 与 `ToolBoxOS.elf`。

```powershell
./tools/build.ps1
./tools/build.ps1 -IdfPath 'D:/ESP/esp-idf' -BuildDirectory 'build-custom'
```

脚本使用 `IDF_PATH`、`IDF_PYTHON_ENV_PATH` 和当前 `PATH`。需要初始化时调用所选 ESP-IDF 的导出脚本。VS Code ESP-IDF 扩展和默认命令行构建都使用 `build-toolbox`。ESP-IDF 安装路径与串口在扩展的用户设置中选择。

### 从 1.1 迁移

先备份重要文件和配置，并核对旧分区表。以下偏移适用于仓库默认的 16 MiB 未加密 1.1 固件。新旧 FAT 物理区域有重叠。请在任何写入前完成备份，并把镜像保存在项目外。原始 Flash 含有 Wi-Fi 等配置。

```powershell
python -m esptool --chip esp32s3 -p PORT read_flash 0xA00000 0x400000 storage-v11.bin
python -m esptool --chip esp32s3 -p PORT read_flash 0x9000 0x4000 nvs-v11.bin
```

在构建目录执行下列命令，用实际备份路径替换 FAT_BACKUP。这样 bootloader、分区表、应用和迁移的 FAT 卷会在同次 USB 写入结束后启动。NVS 偏移保持 0x9000。

```powershell
python -m esptool --chip esp32s3 -p PORT write_flash "@flash_args" 0xBF0000 FAT_BACKUP
```

单 Factory 分区已移除 A/B 双槽 OTA。后续使用 `idf.py -B build-toolbox -p PORT flash monitor` 或仅更新应用的 `app-flash`。挂载失败会保留卷并记录错误。全新空白设备可以用以下步骤准备 4 MiB 空卷，然后将 empty-storage.bin 作为上述 FAT_BACKUP 烧入。已经有数据的设备使用自己的备份。

```powershell
New-Item -ItemType Directory empty-volume
python "$env:IDF_PATH/components/fatfs/wl_fatfsgen.py" empty-volume --partition_size 0x400000 --sector_size 4096 --output_file empty-storage.bin
```

## 同源 LVGL 桌面渲染

Windows 需要 MSVC、CMake、Ninja，Linux 可用宿主 C/C++ 编译器。桌面工程默认使用 `IDF_PATH/components/json/cJSON`，也支持传入包含 `cJSON.c` 和 `cJSON.h` 的路径。

```text
tools/ui-render/build.cmd [输出目录] [cJSON源码目录]
cmake -S tools/ui-render -B build-ui-render -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ui-render --parallel
```

运行构建目录的 `render` 和 `camera_backend_check`。默认渲染模式为 DIRECT，`render full` 可运行 FULL 模式回归。BGRA 抓帧位于可执行文件旁的 `images/`。需要 Pillow 把默认构建目录的抓帧导出到文档中的截图路径。

```bash
python tools/ui-render/export.py
python tools/ui-render/export_extensions.py
```

## MiSans 与图标

```bash
npm ci --prefix tools/font-converter
python tools/generate-fonts.py
python tools/convert-app-icons.py
```

[字体配置](../tools/fonts.json) 记录字号、字重、来源、字符集、BPP、压缩方式和用途。生成器默认使用 `reference/MiSans/ttf` 的 Regular 与 Semibold，也可接收 `--font-dir`。固件和桌面编译共用生成结果。新增中文文案后重新生成字库，应用统一通过 [fonts.h](../main/system/fonts.h) 引用。

静态中文字库由源码字符与固定补充字符生成。20px、26px 保留 GB2312 中文字符和现有 2bpp 配置，覆盖动态天气文字与城市。数值专用字号保留各自的正负号、角度符号与标点。转换器版本由 [package-lock.json](../tools/font-converter/package-lock.json) 固定。原始字体及许可来自 [MiSans 官方页面](https://hyperos.mi.com/font/download)。

抽屉图标的源 PNG 保存在 `output/app-icons`，生成脚本输出 LVGL RGB565 资源。

浏览器原型的 SVG 图标可用 `tools/generate-icons.cjs` 导出。脚本从 Node 的模块路径查找 Playwright，使用其 Chromium，也可通过 `TOOLBOX_CHROMIUM_PATH` 指定已有 Chromium 可执行文件。
