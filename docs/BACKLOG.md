# 开发备忘录

本文档追踪 `p4.lua` 待实现的特性。

## 待实现

### 交互式回调

P4 API 定义了以下 `ClientUser` 回调，部分尚未向 Lua 暴露：

| 回调 | 触发场景 | 优先级 | 说明 |
|---|---|---|---|
| `ErrorPause` | 错误后暂停等待用户确认 | 低 | 自动模式下不需要 |
| `Edit` | 弹编辑器编辑 spec 表单 | 低 | 与 `InputData` 功能重叠，可用 `-i` 替代 |
| `Diff` | `p4 diff` 结果展示 | 低 | 输出数据已在 `text`/`binary` 中 |
| `Merge` | `p4 resolve -m` 三方合并 | 低 | 复杂度高，场景少 |
| `Resolve` | `p4 resolve` 交互式冲突解决 | 中 | 工作流核心但实现复杂 |
| `File` | 读写 workspace 文件 | 低 | 需要文件系统抽象层 |

### ClientApi 能力

| 特性 | 优先级 | 说明 |
|---|---|---|
| `SetBreak` / KeepAlive | 低 | 中断正在执行的命令 |

### 平台

| 特性 | 优先级 | 说明 |
|---|---|---|
| macOS 支持 | 中 | 当前仅 Windows + Linux |
| 打包发布 | 低 | — |
| CI/CD | 低 | — |
