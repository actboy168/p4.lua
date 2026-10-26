# `client:run()` 使用说明

本文档专门说明 `p4.lua` 里的 `client:run(command[, args])` 应该怎么理解，以及如何参考官方 `p4` 命令文档。

## 核心规则

可以把它理解成：

```text
client:run(command, args)
≈
p4 <command> <args...>
```

也就是说：

- `command` 是 **P4 子命令名**
- `args` 是这个子命令后面的 **参数数组**

例如：

```lua
client:run("info")
```

对应：

```bash
p4 info
```

再例如：

```lua
client:run("info", { "-s" })
```

对应：

```bash
p4 info -s
```

## 如何参考官方文档

**可以直接参考官方 `p4` 命令文档。**

当前 `client:run()` 的参数组织方式，就是按官方 `p4` CLI 的命令语法来理解的，只不过把命令名和后续参数拆成了两个 Lua 参数：

- 官方文档里的命令名 -> `command`
- 官方文档里命令名后面的参数 -> `args`

官方命令参考首页：

- <https://help.perforce.com/helix-core/server-apps/cmdref/current/Content/CmdRef/Home-cmdref.html>

例如官方 `p4 info` 文档：

- <https://help.perforce.com/helix-core/server-apps/cmdref/current/Content/CmdRef/p4_info.html>

官方语法写的是：

```text
p4 info [-s]
```

在 `p4.lua` 里就写成：

```lua
client:run("info", { "-s" })
```

## 常见映射示例

### 1. `p4 info`

```lua
local result = assert(client:run("info"))
```

```bash
p4 info
```

### 2. `p4 info -s`

```lua
local result = assert(client:run("info", { "-s" }))
```

```bash
p4 info -s
```

### 3. `p4 files //depot/...`

```lua
local result = assert(client:run("files", { "//depot/..." }))
```

```bash
p4 files //depot/...
```

### 4. `p4 changes -m 10`

```lua
local result = assert(client:run("changes", { "-m", "10" }))
```

```bash
p4 changes -m 10
```

### 5. `p4 print //depot/file.txt`

```lua
local result = assert(client:run("print", { "//depot/file.txt" }))
```

```bash
p4 print //depot/file.txt
```

## 全局选项怎么处理

在官方 CLI 里，经常会看到这种语法：

```text
p4 -p ssl:127.0.0.1:1666 -u alice -c myclient info
```

在 `p4.lua` 里，像 `-p`、`-u`、`-c` 这类**连接/上下文相关的全局选项**，通常不要直接放进 `args`，而是通过 client 配置接口来设置：

```lua
local client = assert(p4.new {
    port = "ssl:127.0.0.1:1666",
    user = "alice",
    client = "myclient",
})

local result = assert(client:run("info"))
```

等价理解为：

```bash
p4 -p ssl:127.0.0.1:1666 -u alice -c myclient info
```

当前更适合通过配置接口处理的项包括：

- `port`
- `user`
- `client`
- `host`
- `password`
- `charset`
- `cwd`
- `prog`
- `ticket_file`
- `trust_file`

## 当前更适合执行哪些命令

当前版本更适合：

- 查询类命令：`info`、`files`、`fstat`、`changes`、`users`、`clients`、`dirs`
- 普通文本输出命令：`print`
- 需要 `-i` 输入 spec 内容的命令，如 `change -i`、`client -i`（string 或 table 输入；table 需要先 `client:protocol("specstring", "")`）
- 不需要 resolve / merge 回调的基础命令

## 当前不适合哪些命令

当前版本暂不适合：

- 需要 resolve / merge 回调的命令
- 依赖当前尚未暴露高级 P4 API 回调能力的命令

## 返回值怎么看

`client:run()` 返回的是一个 Lua table；字段按需惰性创建，没有对应数据时该字段不存在。

```lua
local result = assert(client:run("info"))

-- 无错误时 result.errors 为 nil
if result.errors then
    for _, e in ipairs(result.errors) do
        print(e.code, e.severity, e.msgid, e.message)
    end
end
```

常用字段：

- `errors`：错误输出（无错误时为 `nil`）
- `warnings`：警告输出
- `info`：普通信息输出
- `messages`：按到达顺序的 info/warning/error 消息
- `text`：文本输出
- `binary`：二进制输出
- `stats`：tagged / stat 类输出（需先调用 `client:protocol("tag", "")`）

`errors` / `warnings` 的条目包含 `code`（generic `EV_*`）、`severity`、`message`、`msgid`（稳定消息 ID，可能为 `nil`）、`dict`（消息参数，可能为 `nil`）和 `ids`（所有分层 `ErrorId`，可能为 `nil`）。`messages` 使用相同结构，并按消息到达顺序记录。分类用 `msgid` / `ids`，不要匹配 `message`。

成功判断：`result.errors == nil`。

默认连接不再自动设置 `tag` 协议；需要 `stats` 时先显式调用 `client:protocol("tag", "")`。

## 一个完整例子

```lua
local p4 = require "p4"

local client = assert(p4.new {
    port = "127.0.0.1:1666",
    user = "alice",
    client = "alice_ws",
})

local info = assert(client:run("info", { "-s" }))
local changes = assert(client:run("changes", { "-m", "5" }))

print(info.errors == nil, changes.errors == nil)
```
