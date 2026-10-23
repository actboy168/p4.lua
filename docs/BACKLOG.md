# 开发备忘录

本文档追踪 `p4.lua` 待实现的机制。表格中的优先级是维护者排序。

## 待实现

### Spec

目标：form 文本 ↔ 字段表。

- specdef 缓存与内置回退。
- 输出 tagged 结果时判断 `specdef` / `data` / `specFormatted`，必要时先解析 `data`。
- form 文本 ↔ 字段表：
  - 尾部数字/逗号是索引，前缀是基名。
  - 缺失索引用空位补齐。
  - 同名标量与列表冲突时，标量改名。
  - `attr-` / `openattr-` 开头的键保持原样。
- 原语：`parse_spec` / `format_spec` / `define_spec`。

缺口：`stats` 是扁平字符串表，`depotFile0` 原样保留；没有 `parse_spec` / `format_spec`。

### Resolver 与合并

目标：`p4 resolve` / merge 工作流。

- Resolver：`resolve(mergeInfo)` 处理文本合并，`actionResolve(mergeInfo)` 处理 action resolve。
- merge data：双方路径、base 路径、merge hint 等。
- ClientUser 回调：`Resolve`、`Merge`。

缺口：没有 Resolver，也没有 `Resolve` / `Merge` 回调。

### 取消与线程

取消有两条路径：

- `setbreak(callable)`：命令执行期间轮询，返回 `0` 表示终止。
- 回调返回值：异步回调返回 `false` / `"cancel"` 时停止命令。

线程规则：一个连接一个线程；多连接时注意临时资源清理。Lua 没有 GIL，“不跨线程调用同一个 `lua_State`”是等价约束。

缺口：没有 `setbreak`，也没有回调取消。

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
| `SetVar` / 单命令变量 | 低 | 设置 `limitMap0` 等；只影响下一条命令 |
| `SetTunable` / `GetTunable` | 低 | 读写客户端可调参数 |

### 平台

| 特性 | 优先级 | 说明 |
|---|---|---|
| macOS 支持 | 中 | 当前仅 Windows + Linux |
| 打包发布 | 低 | — |
| CI/CD | 低 | — |
