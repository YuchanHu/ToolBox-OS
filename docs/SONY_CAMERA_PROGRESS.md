# Sony 相机高级控制开发进度

状态：需求及 V4 原型已归档，开发尚未开始。

以 [任务书](SONY_CAMERA_TASK.md) 为规范，以 [V4 HTML](../ui-preview/sony-camera/sony_camera_ui_v4.html) 为视觉交互基准。

| 阶段 | 推荐模型 | 推理强度 | 状态 |
|---|---|---|---|
| 1 项目检查及协议验证 | GPT-6.2 Sol | High | 待开发 |
| 2 ESP32-S3 最小通信 | GPT-6.2 Astra（额度紧张先 Sol） | High | 待开发 |
| 3 连接配置及 UI | GPT-6.2 Sol | Medium | 待开发 |
| 4 拍照、录像及参数 | GPT-6.2 Sol | High | 待开发 |
| 5 机身控制 | GPT-6.2 Sol | High | 待开发 |
| 6 媒体管理 | GPT-6.2 Sol | High | 待开发 |
| 7 自动化摄影 | GPT-6.2 Sol | Medium | 待开发 |
| 8 性能优化及整体验收 | GPT-6.2 Astra（额度紧张先 Sol） | High | 待开发 |

模型名称及可用推理等级以实际 Codex 选择器为准；Extra High 仅用于难解阻塞。

阶段执行统一命令：阅读 docs/SONY_CAMERA_TASK.md、docs/SONY_CAMERA_PROGRESS.md 和 ui-preview/sony-camera/sony_camera_ui_v4.html。本轮仅执行任务书第 N 阶段，检查上一阶段验收证据与阻塞，遵守全部约束，完成相关实现、测试和构建（适用时），更新进度文件，列出实际结果和待实机验收项，完成后停止，不提前执行下一阶段。不擅自推送或刷机。

## 交接记录
每轮追加：阶段、完成项、变更文件、参考版本、证据（区分模拟/实机）、构建命令和结果、资源占用、阻塞、待验收及下一阶段建议。
