# 系统与 Refresh V6 架构

`Runtime` 初始化 HAL 与 Mooncake，执行 Wi-Fi、BLE、亮度、音量及系统配置等命令。亮度、音量的连续队列项采用最新值，保留该段的持久化请求。Launcher 管理前台，系统浮层挂在 `lv_layer_top()`。全局控制中心覆盖前台时，应用仍可处理自身业务。

```text
app_launcher   表盘与抽屉入口
app_stopwatch  秒表和计圈
app_setup      系统设置
app_camera     Sony BLE 遥控
app_counter    计数与 NVS 写入
app_timers     倒计时预设和自定义
app_focus      专注、休息和次数
app_level      校准、冻结和 PIN，level_reading.h 提供测量模型
app_weather    城市、日期切换和请求生命周期
common/       按键、倒计时、提示音、页面事件和延迟保存
```

五个工具应用分别拥有生命周期和业务状态，通过共用页面组件提交渲染状态。天气 HTTPS 句柄由单个 FreeRTOS 请求任务持有。退出标记取消并立即释放页面及前台状态，任务结束当前 DNS/TLS/HTTP 调用后清理资源。重新进入时可以显示缓存，等待上一条连接清理后再发出新的请求。只有当前前台会消费请求结果和写天气缓存。

ESP-IDF 5.5.5 的取消 API 会关闭底层传输，而 TLS 路径中的 `getaddrinfo` 仍可能先阻塞。当前实现把 HTTPS 操作和句柄释放留给同一任务，页面关闭直接标记请求已取消。 [ESP-IDF HTTP Client](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/protocols/esp_http_client.html)

## LVGL、触摸和传输

```text
CST820 采样任务 ──→ 手势历史 ──→ LVGL 输入、动画和 UI 更新
                                           │
                                刷新计时器等待缓冲区可复用
                                           ↓
PSRAM 双缓冲 ←── LVGL 9.5 DIRECT 区域同步及绘制
        │
        └──→ 合并相邻脏区 ──→ DMA 条带 ──→ QSPI 80 MHz ──→ CO5300
```

1. StopWatch 的 CST820 通过 GPIO13 唤醒采样任务。空闲时保留 32 ms 轮询，触摸跟踪以 4 ms 采样。积压时保序存储按下、抬起和取消，以及首次移动和最新两次移动。 [M5Stack StopWatch 引脚表](https://docs.m5stack.com/zh_CN/core/StopWatch)
2. LVGL 线程在统一 GUI 锁下处理输入与动画。有输入或计时器就绪时唤醒，抬手后暂停输入计时器。DMA 读取上一帧时，LVGL 可在另一块缓冲预备一帧，提交由单个显示任务串行完成。
3. 同一帧的多个失效矩形用有界区域表收集，邻近且合并成本合适的区域共用传输窗口。DMA 采用双条带交替打包，RGB565_SWAPPED 字节序直接复制。
4. 第一条 DMA 在 TE 等待前准备。使用最近且未消费的 TE，或在 25 ms 内等候可用边沿。接收相位限定为 500 μs。持续 50 ms 没有待传输帧时关闭 TE 中断。超时后合并待补发区域，静态页面也可恢复。
5. 抽屉复用五个 LVGL 卡片槽。进入表盘时建立解锁缓存。时钟或电量变化后，在最后一个脏区提交前，将已绘制的表盘像素按各自的行跨度转入 RGB565 缓存。控制中心、设置及解锁过渡期间跳过复制。手势或按键动画直接更新两张缓存图的位移、缩放和透明度。

同时只允许一帧借出。显示任务发出的完成信号覆盖 TE 放弃及传输成功两种出口。画面性能与同步正确性可在实机按 [性能验收](PERFORMANCE.md) 测量。

## 1.2 固件与天气

16 MiB Flash 使用一个 Factory 应用分区。USB 完整烧录写入 bootloader、分区表及应用。Storage 挂载失败会记录错误，恢复及首次准备见构建指南。表盘上的单键点击复用 Shell 的解锁补间动画。FFT 应用及无调用方的分析资源已移除。

天气查询键区分 auto、city、adcode，旧 city JSON 延迟兼容读取。城市显示名称来自响应。页面共享只读预报，小时趋势与每日范围使用同一次成功请求的数据。mbedTLS 使用 PSRAM 分配握手及收发缓冲，并通过 ESP-IDF 证书包校验证书。网络线程生成有上限的缓存，前台接受结果后轮换写入 FAT 的两个缓存文件。打开天气时按查询键、序号和 CRC 选择完整记录。挂载或写入失败时使用 NVS，FAT 写入成功后可释放旧 NVS 缓存。扬声器功放随播放启停。
