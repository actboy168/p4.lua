# API 参考

## 快速开始

```lua
local p4 = require "p4"

local client = p4.new {
    port   = os.getenv("P4PORT"),
    user   = os.getenv("P4USER"),
    client = os.getenv("P4CLIENT"),
    prog   = "my app",
}

local result = client:run("info")
for _, stat in ipairs(result.stats) do
    print(stat.serverVersion)
end

client:close()
```

## 模块

### `p4.new([options])`

创建一个 P4 client 对象。失败时返回 `nil, error_message`。

```lua
local client, err = p4.new { port = "server:1666", user = "alice" }
if not client then
    error(err)
end
```

## Client 对象

### 属性

配置项可直接读写（和 P4Python 风格一致）：

```lua
client.port = "server:1666"
print(client.port)
```

支持: `port`, `user`, `client`, `password`, `host`, `charset`, `cwd`, `prog`, `version`, `language`, `ticket_file`, `trust_file`, `enviro_file`, `ignore_file`

未设置的属性返回 `nil`。

### `client:connect()` / `client:close()` / `client:connected()`

连接管理。`run()` 首次调用时自动连接。`port` 必须在连接前设置。

### `client:run(command[, args[, options]])`

执行 P4 命令。失败时返回 `nil, error_message`。

```lua
local r = client:run("info")
local r = client:run("changes", { "-s", "submitted", "-m", "10" })

-- spec 表单输入
local r = client:run("change", { "-i" }, { input = "Change: new\n..." })

-- 交互输入响应
local r = client:run("login", {}, { prompt = "password" })
```

### `client:run_async(command[, args[, options]])`

启动异步命令，返回 tag 对象。数据通过回调流式接收。

```lua
local count = 0
local tag = client:run_async("fstat", { "//depot/..." }, {
    on_stat  = function(stat) count = count + 1 end,
    on_error = function(code, severity, msg) print(msg) end,
})
tag:wait()
```

`s` 支持的回调：

| 回调 | 参数 | 说明 |
|---|---|---|
| `on_stat` | `(stat)` | 每个 tagged 记录 |
| `on_error` | `(code, severity, msg)` | 即时错误 |
| `on_warning` | `(code, msg)` | 即时警告 |
| `on_info` | `(level, data)` | 信息消息 |
| `on_text` | `(text)` | 文本输出 |
| `on_binary` | `(data)` | 二进制输出 |
| `on_input` | `() → return "spec"` | spec 表单输入 |
| `on_prompt` | `(msg, no_echo) → return "rsp"` | 交互输入 |

### `tag:wait()`

等待异步命令完成。返回 `true`（成功）或 `false`（失败）。

## 结果结构

### `run()` 返回

所有数组惰性创建——只有存在对应数据时才会出现在结果中。

```lua
{
    errors   = {},  -- { code, severity, message }[]  (severity 3=error, 4=fatal)
    warnings = {},  -- { code, severity=2, message }[]
    info     = {},  -- { level, data }[]
    text     = {},  -- string[]
    binary   = {},  -- string[]
    stats    = {},  -- table[]  tagged 结构化输出
}
```

判断成功：`if result.errors`（无错误时为 nil）。

### `tag:wait()` 返回

`true` 或 `false`。数据全部通过回调接收。

### stats 字段名

`stats` 中的键名来自 P4 tagged 输出协议。CLI 对比：

```bash
$ p4 -Ztag changes -m 1
... change 100976
... user yixiandeng
... desc --story=132471656 ...
```

各命令的字段参考 [P4 命令文档](https://help.perforce.com/helix-core/server-apps/cmdref/current/Content/CmdRef/Home-cmdref.html)。

多值字段使用数字后缀: `depotFile0`, `depotFile1`, `action0`, `action1` 等。

## 错误处理

```lua
local r = client:run("changes", { "-m", "10" })
if not r then
    -- run() 本身失败
    return
end

if r.errors then
    for _, e in ipairs(r.errors) do
        print(string.format("[%d] sev=%d %s", e.code, e.severity, e.message))
    end
end

if r.warnings then
    for _, w in ipairs(r.warnings) do
        print(string.format("[WARN %d] %s", w.code, w.message))
    end
end
```

## 配置项

| 配置项 | 说明 |
| --- | --- |
| `port` | 服务器地址 |
| `user` | 用户名 |
| `client` | 工作空间名 |
| `password` | 密码 / ticket |
| `host` | 主机名 |
| `charset` | 字符集 |
| `cwd` | 工作目录 |
| `prog` | 程序名（标识用途） |
| `version` | 版本号 |
| `language` | 语言 |
| `ticket_file` | ticket 文件路径 |
| `trust_file` | trust 文件路径 |
| `enviro_file` | 环境文件路径 |
| `ignore_file` | ignore 文件路径 |
