# 开发备忘录

本文档追踪 `p4.lua` 待实现的机制。表格中的优先级是维护者排序。

## 待实现

### Spec

目标：form 文本 ↔ 字段表。

已完成：

- `client:run` / `client:run_async` 的 `options.input` / `on_input` 支持 Lua 表；
  由 binding 使用服务器 `specdef` + `Spec` / `SpecDataTable` 自动生成 form。
- 支持标量、list 字段（`View` / `AltRoots` / `ChangeView`）和多行 text（`Description`）。
- 首次连接前启用 `specstring` 后，`client -i`、`change -i` 等命令可直接传表。

待实现：

- `parse_spec` / `format_spec` / `spec_fields` / `define_spec`：form → 字段表、
  字段元数据。
- specdef 缓存与内置回退。
- tagged 输出中 `specdef` / `data` / `specFormatted` 的处理；当前 `stats` 仍是
  扁平字符串表，`depotFile0` 原样保留。
- 反向解析仍需要 Lua 侧的 `SpecData` 实现；前向 table → form 已由
  `StrBufDict` + `SpecDataTable` 覆盖。

### Resolver 与合并

目标：`p4 resolve` / merge 工作流。

- Resolver：`resolve(mergeInfo)` 处理文本合并，`actionResolve(mergeInfo)` 处理 action resolve。
- `ClientResolveA` 的 `preview` 参数；`ClientMerge` 的 `AutoResolve()`、
  `MergeStatus`、`CMF_*` 选项。
- merge data：双方路径、base 路径、merge hint 等。
- ClientUser 回调：`Resolve(ClientMerge*)`、`Resolve(ClientResolveA*, int preview)`。

缺口：没有 Resolver，也没有 `Resolve` / `Merge` 回调。

### 取消与线程

原生取消只有一条路径：

- `setbreak(callable)`：包装 `KeepAlive`，命令执行期间轮询 `IsAlive()`；
  返回 `0` 表示终止。`PollMs()` 默认 500ms。
- `SetBreak()` 必须在 `Init()` 之后调用。

回调返回值取消不是 P4 API 的原生能力。如果要做“回调返回 `false` / `"cancel"` 就停止”，
只能让回调设置一个标志，再由 `KeepAlive::IsAlive()` 读取；它不能立即打断阻塞中的网络读。

线程规则：一个连接一个线程；多连接时注意临时资源清理。Lua 没有 GIL，“不跨线程调用同一个 `lua_State`”是等价约束。

缺口：没有 `setbreak`，也没有基于标志的回调取消。

### 其他 ClientUser 回调

| 回调 | 触发场景 | 优先级 | 说明 |
|---|---|---|---|
| `ErrorPause` | 错误后暂停等待用户确认 | 低 | 自动模式下不需要 |
| `Edit` | 弹编辑器编辑 spec 表单 | 低 | 与 `InputData` 功能重叠，可用 `-i` 替代 |
| `Diff` | `p4 diff` 结果展示 | 低 | 输出数据已在 `text` / `binary` 中 |
| `File` | 读写 workspace 文件 | 低 | 需要文件系统抽象层 |

`Resolve` / `Merge` 见“Resolver 与合并”。

### 小项

| 项 | 优先级 | 说明 |
|---|---|---|
| `SetVar` / 单命令变量 | 低 | 设置 `limitMap0` 等；只影响下一条命令。典型变量包括 `tag`、`enableStreams`、`enableGraph`、`maxResults`、`maxScanRows`、`maxLockTime`、`progress` |
| `SetTunable` / `GetTunable` | 低 | 读写客户端可调参数 |

### 平台

| 特性 | 优先级 | 说明 |
|---|---|---|
| macOS 支持 | 中 | 当前仅 Windows + Linux |
| 打包发布 | 低 | — |
| CI/CD | 低 | — |
