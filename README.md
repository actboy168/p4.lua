# p4.lua

一个基于 **Helix Core P4 C/C++ API** 的 Lua 绑定库。

## 当前状态

- 支持平台：Windows、Linux / WSL
- 默认 Lua ABI：`lua55`
- Windows：使用下载的 P4 SDK 和静态 OpenSSL 1.1.1
- Linux：使用下载的 P4 SDK，并链接系统 OpenSSL 3

生成物：

- Windows：`build\windows\bin\p4.dll`
- Linux：`build/linux/bin/p4.so`

功能状态说明见：

- `docs\FEATURES.md`
- `docs\RUN.md`

## 依赖要求

需要以下环境：

- Windows
- Visual Studio 2022 / MSVC
- `luamake`
- 构建前需要先下载依赖
- Linux / WSL 构建需要系统 OpenSSL 开发库

## 构建

1. 下载依赖：

```powershell
luamake lua compile\download_deps.lua
```

2. 构建：

```powershell
luamake
```

下载脚本会把依赖解压/安装到：

- `deps\p4api_vs2022_static_openssl1.1.1`
- `deps\openssl-full-1.1.1w`

在 Linux / WSL 下执行同一个命令：

```bash
luamake lua compile/download_deps.lua
luamake
```

## 运行测试

```powershell
luamake lua test\load.lua
```

Linux / WSL：

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

print(result.ok)
print(result.error_count)
```

更完整的示例见：

- `examples\info.lua`

