# 功能状态

本文档描述 `p4.lua` 当前**已实现**和**未实现**的特性，方便判断这版绑定可以做什么、还缺什么。

## 已实现的特性

### 1. 构建与运行时

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| `luamake` 构建 | 已实现 | 使用 `make.lua` 构建 `build\bin\p4.dll` |
| Windows + MSVC 支持 | 已实现 | 当前目标平台是 Windows / VS2022 / x64 |
| P4 SDK 接入 | 已实现 | 已接入本地 P4 C/C++ API SDK |
| OpenSSL 静态链接 | 已实现 | 当前使用 `deps\openssl-full-1.1.1w` 中的静态库 |
| Lua 模块导出 | 已实现 | 当前导出模块名为 `p4` |

### 2. Lua 模块基础接口

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| `p4.new([options])` | 已实现 | 创建 client 对象 |
| `client:configure(options)` | 已实现 | 批量设置 client 参数 |
| `client:set(key, value)` | 已实现 | 设置单个参数 |
| `client:get(key)` | 已实现 | 读取单个参数 |
| 访问器风格配置 | 已实现 | 如 `client:port()` / `client:user()` |
| `client:connect()` | 已实现 | 显式建立连接 |
| `client:close()` | 已实现 | 显式关闭连接 |
| `client:connected()` | 已实现 | 查询当前连接状态 |
| `client:run(command[, args])` | 已实现 | 执行基础非交互 P4 命令 |

### 3. 已支持的配置项

以下配置项当前都可通过 `new/configure/set` 和访问器方式使用：

- `charset`
- `client`
- `cwd`
- `enviro_file`
- `host`
- `ignore_file`
- `language`
- `password`
- `port`
- `prog`
- `ticket_file`
- `trust_file`
- `user`
- `version`

### 4. 命令结果建模

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| `ok` | 已实现 | 标识命令是否成功 |
| `error_count` | 已实现 | 返回 P4 API 错误数量 |
| `info` | 已实现 | 收集 `OutputInfo` / `Message` |
| `text` | 已实现 | 收集 `OutputText` |
| `binary` | 已实现 | 收集 `OutputBinary` |
| `errors` | 已实现 | 收集错误输出 |
| `stats` | 已实现 | 收集 `OutputStat` 结果 |

### 5. `client:run()` 当前适用的命令范围

`client:run(command[, args])` 当前没有对命令名做白名单限制；从接口上说，传入的就是普通 `p4` 命令名。

更详细的使用方式、CLI 对应关系和官方文档映射见：

- `docs\RUN.md`

当前更适合的命令：

- 查询类命令：`info`、`files`、`fstat`、`changes`、`users`、`clients`、`dirs`
- 文本输出类命令：`print`
- 不需要交互输入的基础命令

当前不适合的命令：

- 需要 spec 输入的命令，如 `change -i`、`client -i`
- 需要 `Prompt` / `InputData` / `Resolve` 回调的命令
- 需要人工处理冲突或编辑器交互的命令
- 依赖当前尚未暴露高级 P4 回调能力的命令

### 6. 示例与基础验证

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| 模块加载 smoke test | 已实现 | `test\load.lua` |
| 基础示例 | 已实现 | `examples\info.lua` |

## 未实现的特性

### 1. 交互式能力

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| `Prompt` 回调 | 未实现 | 当前不支持交互式输入提示 |
| `InputData` 回调 | 未实现 | 当前不支持 `p4 change -i` 等 spec 输入 |
| `Resolve` / `Merge` | 未实现 | 当前不支持冲突处理和交互式 resolve |
| `Edit` / `Diff` / `Help` | 未实现 | 未向 Lua 暴露对应交互行为 |

### 2. 更完整的 P4 绑定面

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| 协议设置接口 | 未实现 | 未暴露 `SetProtocol` / `GetProtocol` |
| 更细粒度连接参数控制 | 未实现 | 仅支持当前已开放的配置字段 |
| 多线程 /线程初始化策略 | 未实现 | 当前按单线程常规使用场景实现 |
| KeepAlive / 中断控制 | 未实现 | 未暴露 `SetBreak` 能力 |
| 异步命令接口 | 未实现 | 未暴露 `RunTag` / `WaitTag` |

### 3. 结果与错误模型增强

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| 更细粒度 tagged 输出建模 | 未实现 | 当前 `stats` 直接返回键值表数组 |
| 错误码映射 | 未实现 | 当前主要返回格式化后的文本错误 |
| 二进制输出的零拷贝处理 | 未实现 | 当前统一按 Lua 字符串返回 |
| 更丰富的返回元信息 | 未实现 | 例如 severity、generic code、fatal 标记等 |

### 4. 平台与发布能力

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| Linux 支持 | 未实现 | 当前仅覆盖 Windows |
| macOS 支持 | 未实现 | 当前仅覆盖 Windows |
| Lua 版本矩阵测试 | 未实现 | 当前固定使用 `lua55` |
| 打包发布脚本 | 未实现 | 当前只有本地构建流程 |

## 当前适用范围

当前版本适合：

- 连接 P4 服务
- 执行基础非交互命令
- 从 Lua 中获取结构化结果
- 做 MVP 级别的自动化和集成验证

当前版本暂不适合：

- 需要 spec 输入的命令
- 需要人工交互的命令
- 复杂 resolve / merge 工作流
- 多平台发布场景

## 建议的后续优先级

1. 支持 spec 输入
2. 补充协议与 tagged 输出能力
3. 暴露更完整的错误/状态模型
4. 处理交互式命令
5. 补齐跨平台支持
