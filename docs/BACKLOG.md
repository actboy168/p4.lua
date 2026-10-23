# 开发备忘录

本文档追踪 `p4.lua` 待实现的机制。表格中的优先级是维护者排序。

## 待实现

### Spec

目标：form 文本 ↔ 字段表。

- 直接复用 SDK 的 `Spec` / `SpecData` / `SpecDataTable`；Lua 侧需要一个
  `SpecData` 子类，把标量映射为 string、列表映射为 array。
- specdef 缓存与内置回退；内置定义覆盖
  `branch/change/client/depot/group/job/label/ldap/license/protect/remote/stream/user`。
- 输出 tagged 结果时判断 `specdef` / `data` / `specFormatted`，必要时先解析 `data`；
  处理时跳过 `func` / `specFormatted`。
- form 文本 ↔ 字段表：
  - 尾部数字/逗号是索引，前缀是基名。
  - 缺失索引用空位补齐。
  - 同名标量与列表冲突时，标量改名（追加 `s`）。
  - `attr-` / `openattr-` 开头的键保持原样。
- 原语：`parse_spec` / `format_spec` / `define_spec` / `spec_fields`。
  `spec_fields` 返回字段的 type/opt/fmt/seq/values/presets 等元数据。

缺口：`stats` 是扁平字符串表，`depotFile0` 原样保留；没有 `parse_spec` /
`format_spec` / `spec_fields`。

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
