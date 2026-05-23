# API 参考

## 快速开始

```lua
local p4 = require "p4"

local client, err = p4.new {
    port   = os.getenv("P4PORT"),
    user   = os.getenv("P4USER"),
    client = os.getenv("P4CLIENT"),
}

assert(client, err)

local result, run_err = client:run("info")
assert(result, run_err)

for _, stat in ipairs(result.stats) do
    print(stat.serverVersion, stat.serverDate)
end

client:close()
```

## 模块

### `p4.new([options])`

创建一个 P4 client 对象。

- `options`: 可选 table，配置连接参数（见[配置项](#配置项)）
- 返回值: `client` 对象，失败时返回 `nil, error_message`

```lua
local client, err = p4.new {
    port = "server:1666",
    user = "alice",
    prog = "my tool",
}
```

## Client 对象

### 属性

配置项可以直接通过属性读写（和 P4Python 风格一致）：

```lua
-- 写
client.port = "server:1666"
client.user = "alice"

-- 读
print(client.port)    -- "server:1666"
print(client.user)    -- "alice"
```

支持的属性: `port`, `user`, `client`, `password`, `host`, `charset`, `cwd`, `prog`, `version`, `language`, `ticket_file`, `trust_file`, `enviro_file`, `ignore_file`

动态 key 用 `client[key]` 同样有效：`client["port"] = "x"` / `print(client["port"])`。

### `client:connect()`

建立 P4 连接。`run()` 会在首次调用时自动连接，通常不需要手动调用。

```lua
client:connect()
```

### `client:close()`

关闭 P4 连接。

```lua
client:close()
```

### `client:connected()`

查询当前连接状态。返回 `true` / `false`。

### `client:run(command[, args[, options]])`

执行 P4 命令。

- `command`: P4 子命令名，如 `"info"`、`"changes"`、`"describe"`
- `args`: 参数数组，如 `{ "-s" }`、`{ "-m", "10" }`
- `options`: 可选 table，`input` = spec 表单数据，`prompt` = 交互输入响应
- 返回值: 结果 table，失败时返回 `nil, error_message`

```lua
-- p4 info
local r = client:run("info")

-- p4 changes -s submitted -m 10
local r = client:run("changes", { "-s", "submitted", "-m", "10" })

-- p4 describe -s 12345
local r = client:run("describe", { "-s", "12345" })

-- p4 login (自动提供密码)
local r = client:run("login", {}, { prompt = "mypassword" })

-- p4 change -i / client -i (提交 spec 表单)
local r = client:run("client", { "-o" })  -- 读取当前表单
local form = r.stats[1]
-- 修改 form ...
local r2 = client:run("client", { "-i" }, { input = form_str })
```

更多用法见 `docs/RUN.md`。

## 配置项

创建 client 时，P4 API 会自动从 P4 配置文件/注册表读取以下设置。在 `p4.new()` 中传入的值会覆盖默认值。

| 配置项 | 说明 |
| --- | --- |
| `port` | P4 服务器地址，如 `"server:1666"` |
| `user` | P4 用户名 |
| `client` | 工作空间名 |
| `password` | 密码 / ticket |
| `host` | 客户端主机名 |
| `charset` | 字符集，如 `"utf8"` |
| `cwd` | 工作目录 |
| `prog` | 客户端程序名（标识用途） |
| `version` | 客户端版本号 |
| `language` | 语言 |
| `ticket_file` | ticket 文件路径 |
| `trust_file` | trust 文件路径 |
| `enviro_file` | 环境文件路径 |
| `ignore_file` | ignore 文件路径 |

## 结果结构

`client:run()` 返回的 table：

```lua
{
    ok          = true,       -- 命令是否成功（无 fatal/error）
    error_count = 0,          -- P4 API 错误计数
    errors      = {},         -- {code=n, severity=n, message="..."}[] 错误 (severity 3/4)
    warnings    = {},         -- {code=n, severity=2, message="..."}[] 警告
    info        = {},         -- {level = n, data = "..."}[] 信息消息
    text        = {},         -- string[] 文本输出
    binary      = {},         -- string[] 二进制输出
    stats       = {},         -- table[] 结构化 tagged 输出
}
```

**`stats` 是命令结果的主要来源。** 比如：

```lua
-- p4 info
local r = client:run("info")
-- r.stats[1].serverVersion, r.stats[1].serverDate, ...

-- p4 changes -l
local r = client:run("changes", { "-l", "-m", "5" })
for _, c in ipairs(r.stats) do
    print(c.change, c.user, c.time, c.desc)
end

-- p4 describe -s 12345
local r = client:run("describe", { "-s", "12345" })
-- r.stats[1].desc, r.stats[1].user, r.stats[1].depotFile0, ...
```

### 如何理解 stats 返回的字段名

`stats` 里的键名来自 P4 服务器的 tagged 输出协议。对比 CLI：

```bash
$ p4 -Ztag changes -m 1          # -Ztag 显示 tagged 格式
... change 100976
... time 1779457615
... user yixiandeng
... desc --story=132471656 ...
```

这个 tagged 输出就是 `stats` 的来源。各命令返回的字段参见 [P4 命令文档](https://help.perforce.com/helix-core/server-apps/cmdref/current/Content/CmdRef/Home-cmdref.html) 中对应命令的 `-Ztag` 输出说明。

多值字段（如 `describe` 的 `depotFile`）使用数字后缀: `depotFile0`, `depotFile1`, `action0`, `action1` 等。

## 错误处理

错误、警告、信息分别在 `errors[]`、`warnings[]`、`info[]` 三个数组中。

```lua
local r, err = client:run("changes", { "-m", "10" })
if not r then
    io.stderr:write("run failed: " .. err .. "\n")
    return
end

-- 警告不影响 ok，单独在 warnings[]
for _, w in ipairs(r.warnings) do
    print(string.format("[WARN %d] %s", w.code, w.message))
end

-- 错误在 errors[]，severity 区分 3(error) / 4(fatal)
if not r.ok then
    for _, e in ipairs(r.errors) do
        print(string.format("[%d] severity=%d: %s", e.code, e.severity, e.message))
    end
    return
end
```

## 连接管理

`run()` 会在首次调用时自动连接，并在检测到断线时自动重连。手动管理：

```lua
-- 显式控制
client:connect()
assert(client:connected())
-- ... 使用 ...
client:close()

-- 自动模式（推荐）
local r = client:run("info")  -- 自动连接
```

需要注意：`port` 必须在 `connect()` 之前设置。已连接状态下修改 `port` 会报错。
