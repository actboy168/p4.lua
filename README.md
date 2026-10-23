# p4.lua

一个基于 **Helix Core P4 C/C++ API** 的 Lua 绑定库。

## 构建

1. 下载依赖：

```powershell
luamake lua compile\download_deps.lua
```

2. 构建：

```bash
luamake lua compile/download_deps.lua
luamake
```

## 运行测试

```bash
luamake lua test/load.lua
```

## 快速开始

```lua
local p4 = require "p4"

local client, err = p4.new {
    port = os.getenv "P4PORT",
    user = os.getenv "P4USER",
    client = os.getenv "P4CLIENT",
    password = os.getenv "P4PASSWD",
    cwd = os.getenv "P4CWD",
    prog = "p4.lua example",
}

assert(client, err)

local result, run_err = client:run("info")
assert(result, run_err)

-- 无错误时 result.errors 为 nil
if result.errors then
    print(#result.errors, result.errors[1].message)
end
```

更完整的示例见：

- `examples\info.lua`

