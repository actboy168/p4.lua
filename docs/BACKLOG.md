# 开发备忘录

本文档追踪 `p4.lua` 已实现和待实现的特性，面向开发者。

## 已实现

### 构建与运行时

- Windows + MSVC (VS2022 / x64) 构建
- Linux + GCC 构建
- P4 C/C++ API SDK 接入 (2025.2)
- OpenSSL 静态链接
- Lua 5.5 模块导出 (`p4`)

### Lua 接口

- `p4.new([options])` — 创建 client
- `client.key = value` / `client.key` — 属性读写（14 个配置项）
- `client:connect()` / `client:close()` / `client:connected()` — 连接管理
- `client:run(command[, args[, options]])` — 执行 P4 命令，`options.input` 提供 spec 表单数据（`change -i` / `client -i` 等）

### 配置项

`charset`, `client`, `cwd`, `enviro_file`, `host`, `ignore_file`, `language`, `password`, `port`, `prog`, `ticket_file`, `trust_file`, `user`, `version`

### 命令结果

`ok`, `error_count`, `info[]`, `text[]`, `binary[]`, `errors[]`, `warnings[]` (`{code, severity, message}`), `stats[]`

### 协议

- 默认启用 tagged 输出 (`SetProtocol("tag", "")`)，`stats` 返回结构化键值表

## 待实现

### 交互式能力

| 特性 | 优先级 | 说明 |
| --- | --- | --- |
| `Prompt` 回调 | 高 | 交互式输入提示 |
| `Resolve` / `Merge` | 中 | 冲突处理和交互式 resolve |
| `Edit` / `Diff` / `Help` | 低 | 对应交互回调 |

### P4 绑定面

| 特性 | 优先级 | 说明 |
| --- | --- | --- |
| 多线程 / 线程初始化策略 | 低 | 当前按单线程常规使用场景实现 |
| `SetBreak` / KeepAlive | 低 | 未暴露中断控制能力 |
| `RunTag` / `WaitTag` (异步) | 低 | 未暴露异步命令接口 |

### 结果与错误模型

| 特性 | 优先级 | 说明 |
| --- | --- | --- |
| 二进制输出的零拷贝处理 | 低 | 当前统一按 Lua 字符串返回 |

### 平台与发布

| 特性 | 优先级 | 说明 |
| --- | --- | --- |
| macOS 支持 | 中 | 当前仅覆盖 Windows + Linux |
| Lua 版本矩阵测试 | 低 | 当前固定使用 `lua55` |
| 打包发布脚本 | 低 | 当前只有本地构建流程 |
| CI/CD | 低 | — |

## 建议的开发顺序

1. **`Prompt` 回调** — 交互式输入提示
2. **`Resolve` / `Merge` 回调** — 冲突处理和交互式 resolve
3. **跨平台补齐** (macOS)
4. **更多 P4 绑定面** (异步命令、KeepAlive 等)
