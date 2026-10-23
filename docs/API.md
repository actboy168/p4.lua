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

client:protocol("tag", "")  -- 需要 result.stats 时必须先开启 tagged 协议
local result = client:run("info")
for _, stat in ipairs(result.stats or {}) do
    print(stat.serverVersion)
end

client:close()
```

> 连接不再自动设置 `tag` 协议。需要 `stats` 时显式调用 `client:protocol("tag", "")`；否则命令按未标记文本返回。

## 模块

### `p4.new([options])`

创建一个 P4 client 对象。失败时返回 `nil, error_message`。

```lua
local client, err = p4.new { port = "server:1666", user = "alice" }
if not client then
    error(err)
end
```

### `p4.map()`

创建空的客户端路径映射对象，不需要连接服务器。

```lua
local m = p4.map()
m:insert("//depot/main/...", "//ws/main/...")
m:insert("//depot/main/secret/...", "//ws/main/secret/...", p4.MAP_EXCLUDE)

print(m:translate("//depot/main/a.c"))      -- //ws/main/a.c
print(m:translate("//ws/main/a.c", p4.MAP_RIGHT_LEFT))  -- //depot/main/a.c
```

`type` 取 `p4.MAP_INCLUDE` / `p4.MAP_EXCLUDE` / `p4.MAP_OVERLAY` / `p4.MAP_ONE_TO_MANY`，默认 `p4.MAP_INCLUDE`。这些常量直接对应 SDK 的 `MapType` 枚举。

| 方法 | 说明 |
| --- | --- |
| `m:insert(lhs, rhs[, type])` | 插入一条映射 |
| `m:translate(path[, direction])` | 返回映射后的路径；不匹配或排除时返回 `nil` |
| `m:translate_array(path[, direction])` | 返回路径数组；一对多映射可能有多个结果 |
| `m:reverse()` | 返回左右交换后的新映射 |
| `m:join(other)` | 返回两张映射组合后的新映射 |
| `m:entries()` | 返回 `{ left, right, type }` 数组 |
| `m:count()` | 条目数量 |
| `m:clear()` | 清空映射 |

`direction` 省略时默认 `p4.MAP_LEFT_RIGHT`；反向用 `p4.MAP_RIGHT_LEFT`。这两个常量直接对应 SDK 的 `MapDir` 枚举。

解析 `p4 client -o` 的 `View` 行由使用者完成，例如：

```lua
local function add_view_line(m, line)
    local lhs, rhs = line:match("^(%S+)%s+(.+)$")
    if not lhs then return end
    local type = p4.MAP_INCLUDE
    local c = lhs:sub(1, 1)
    if c == "-" then
        type, lhs = p4.MAP_EXCLUDE, lhs:sub(2)
    elseif c == "+" then
        type, lhs = p4.MAP_OVERLAY, lhs:sub(2)
    elseif c == "&" then
        type, lhs = p4.MAP_ONE_TO_MANY, lhs:sub(2)
    end
    m:insert(lhs, rhs, type)
end
```

## Client 对象

### 属性

配置项可直接读写：

```lua
client.port = "server:1666"
print(client.port)
```

支持: `port`, `user`, `client`, `password`, `host`, `charset`, `cwd`, `prog`, `version`, `language`, `ticket_file`, `trust_file`, `enviro_file`, `ignore_file`

未设置的属性返回 `nil`。

### `client:connect()` / `client:close()` / `client:connected()`

连接管理。`run()` 首次调用时自动连接。`port` 必须在连接前设置。

### 环境与配置

| 方法 | 说明 |
| --- | --- |
| `client:env(name)` | 读取合并后的 P4 环境值（包括 `P4CONFIG`）；未设置返回 `nil` |
| `client:p4config_file()` | 当前 `P4CONFIG` 值或文件路径；没有时返回 `nil` |
| `client:p4config_files()` | 已加载的配置文件列表（可能为空表） |
| `client:protocol(name[, value])` | 读取协议值（不存在时 `nil`）；传 `value` 时设置并返回 `true` |
| `client:is_ignored(path)` | 路径是否命中 P4 ignore 规则，返回布尔值 |

说明：

- `env()` 读取的是环境层，不代表显式属性（`client.port` 等）的最终值；它和连接共用同一个 `Enviro`，修改 `cwd` 会重新加载环境。
- `p4config_file()` / `p4config_files()` 反映当前 `cwd` 下已加载的配置。
- `protocol` 可读写，包括 `tag`；协议值应在首次 `run` / `connect` 前设置，连接后设置可能不生效。改 `tag` 会导致 `result.stats` 缺失，由使用者负责。
- `is_ignored` 的路径原样传入，建议传绝对路径。
- `cwd` 赋值只调用 `ClientApi::SetCwd`，由 SDK 重新加载 `P4CONFIG`，绑定不做额外处理。
- 不提供 `set_env` / `set_var`；连接配置请使用 `port` / `user` / `client` 等属性。

### `client:run(command[, args[, options]])`

执行 P4 命令。失败时返回 `nil, error_message`。

```lua
local r = client:run("info")
local r = client:run("changes", { "-s", "submitted", "-m", "10" })

-- spec 表单输入：string 原样传入
local r = client:run("change", { "-i" }, { input = "Change: new\n..." })

-- spec 表单输入：table 自动格式化为 form
client:protocol("specstring", "")
local r = client:run("client", { "-i" }, { input = {
    Client = "foo",
    Root = "C:/ws/foo",
    View = { "//depot/... //foo/..." },
} })

-- 交互输入响应
local r = client:run("login", {}, { prompt = "password" })
```

`input` 可以是 string 或 table：

- string 原样作为 form / stdin。
- table 使用服务器 `specdef` + SDK `Spec` 自动格式化；需要先启用 `specstring` 协议（`p4sdk.connect` 会自动启用）。
- table 只映射 `specdef` 中定义的字段；未知 key 会被忽略。
- `options.input` 不是 table/string 时不会自动转换。

### `client:run_async(command[, args[, callbacks]])`

启动异步命令，返回 tag 对象。数据通过回调流式接收。

```lua
local count = 0
client:protocol("tag", "")  -- on_stat 需要 tagged 输出；绑定不会自动开启
local tag = client:run_async("fstat", { "//depot/..." }, {
    on_stat  = function(stat) count = count + 1 end,
    on_error = function(code, severity, msg) print(msg) end,
})
tag:wait()
```

`on_stat` 只在 tagged 输出启用时触发；绑定不会自动设置。需要时在首次 `run` / `connect` 前调用 `client:protocol("tag", "")`。没有 tagged 输出时，记录会走 `on_info` / `on_text`。

`callbacks` 支持的回调：

| 回调 | 参数 | 说明 |
|---|---|---|
| `on_stat` | `(stat)` | 每个 tagged 记录 |
| `on_error` | `(code, severity, msg, msgid, dict, ids)` | 即时错误 |
| `on_warning` | `(code, msg, msgid, dict, ids)` | 即时警告 |
| `on_info` | `(level, data, msgid, dict, ids, is_message)` | 信息消息；`is_message` 区分 `ClientUser::Message` 和普通 `OutputInfo` |
| `on_text` | `(text)` | 文本输出 |
| `on_binary` | `(data)` | 二进制输出 |
| `on_input` | `() → return "spec" or table` | spec 表单输入；table 需要 `specstring` |
| `on_prompt` | `(msg, no_echo) → return "rsp"` | 交互输入 |
| `on_progress_init` | `(kind)` | 操作类型，`CPT_*` 整数值 |
| `on_progress_description` | `(description, units)` | 描述和单位，`CPU_*` 整数值 |
| `on_progress_total` | `(total)` | 总量 |
| `on_progress_update` | `(position)` | 当前位置；返回值忽略 |
| `on_progress_done` | `(fail)` | 结束，`fail` 为布尔值 |

Progress 回调只用于 `run_async`；`run()` 不提供进度。不传任何 `on_progress_*` 字段时，Progress 不启用，不会创建进度对象。`kind` / `units` 直接传 `clientprog.h` 的整数值，不做字符串转换。

### `tag:wait()`

等待异步命令完成。返回 `true`（成功）或 `false`（失败）。只有 severity >= 3 的错误/致命消息算失败；warning 不会让 `wait()` 返回 `false`。

应在丢弃 tag 前调用 `wait()`。如果未等待就让 tag 被 GC，`__gc` 会先等待命令结束再释放，可能阻塞；重复调用返回第一次的结果。

## 结果结构

### `run()` 返回

所有数组惰性创建——只有存在对应数据时才会出现在结果中。

```lua
{
    errors   = {},  -- { code, severity, message, msgid, dict, ids }[]  (severity 3=error, 4=fatal)
    warnings = {},  -- { code, severity=2, message, msgid, dict, ids }[]
    info     = {},  -- { level, data }[]
    messages = {},  -- 按到达顺序的 info/warning/error 消息
    text     = {},  -- string[]
    binary   = {},  -- string[]
    stats    = {},  -- table[]  tagged 结构化输出
}
```

判断成功：`result.errors == nil`（无错误时该字段不存在）。结果中**没有** `ok` 或 `error_count` 字段。

`errors` / `warnings` 的条目字段：

| 字段 | 说明 |
|---|---|
| `code` | generic 错误码（`errornum.h` 的 `EV_*`），表示错误大类 |
| `severity` | `1=info, 2=warning, 3=failed, 4=fatal` |
| `message` | 格式化后的文本；受 `P4LANGUAGE` 影响，不要用它做分类 |
| `msgid` | `GetId(0)` 的 `ErrorId::UniqueCode()`；跨语言稳定的消息 ID，可能为 `nil` |
| `dict` | 消息参数表（`Error::GetDict()`）；没有参数时为 `nil` |
| `ids` | 所有分层 `ErrorId` 的数组；每项含 `code`（完整 ErrorId）、`msgid`、`generic`、`severity`、`subsystem`、`subcode`、`args`、`fmt`，可能为 `nil` |

`messages` 与 `errors`/`warnings` 使用相同条目结构；`info` 来源的条目还带 `level`。它只记录 `ClientUser::HandleError` / `ClientUser::Message` 的消息，不包含 `OutputInfo` 的普通输出。

分类应使用 `msgid`（或 `ids` 中的条目）；`message` 只用于展示。例如：

```lua
local MSG_NO_SUCH_FILE = 0x1977
local MSG_TOO_MANY_FILES = 0x19f4

for _, e in ipairs(r.warnings or {}) do
    if e.msgid == MSG_NO_SUCH_FILE then
        -- //depot/... - no such file(s).
    end
end
for _, e in ipairs(r.errors or {}) do
    if e.msgid == MSG_TOO_MANY_FILES then
        local limit = e.dict and e.dict.maxOpenFiles
    end
end
```

### `tag:wait()` 返回

`true` 或 `false`。数据全部通过回调接收。

### stats 字段名

`stats` 只在开启 `tag` 协议时产生。先调用 `client:protocol("tag", "")`，再执行命令。

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
        print(string.format("[%d/%s] sev=%d %s", e.code, tostring(e.msgid), e.severity, e.message))
    end
end

if r.warnings then
    for _, w in ipairs(r.warnings) do
        print(string.format("[WARN %d/%s] %s", w.code, tostring(w.msgid), w.message))
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
