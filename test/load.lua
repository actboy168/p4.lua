local sep = package.config:sub(1, 1)
local ext = sep == "\\" and "dll" or "so"

package.cpath = table.concat({
    "build/windows/bin/?." .. ext,
    "build/linux/bin/?." .. ext,
    package.cpath,
}, ";")

local p4 = require "p4"

assert(type(p4) == "table")
assert(type(p4.new) == "function")

local client, err = p4.new {
    prog = "p4.lua smoke test",
}

assert(client, err)
assert(client:connected() == false)
assert(client.prog == "p4.lua smoke test")

-- 空命令在连接前就被拒绝
local result, run_err = client:run("")
assert(result == nil)
assert(run_err == "command must not be empty")

local tag, async_err = client:run_async("")
assert(tag == nil)
assert(async_err == "command must not be empty")

-- 非字符串参数报错
local ok, arg_err = pcall(function()
    client:run("info", { {} })
end)
assert(not ok)
assert(tostring(arg_err):find("argument 1 must be a string", 1, true), tostring(arg_err))

-- 失败路径也要释放 options 引用
local before = collectgarbage("count")
for _ = 1, 200 do
    client:run("", nil, { input = string.rep("x", 64 * 1024) })
end
collectgarbage("collect")
collectgarbage("collect")
local after = collectgarbage("count")
assert(after - before < 1024, ("options ref leak: %.1f KB"):format(after - before))

-- close 幂等
assert(client:close())
assert(client:close())

-- 环境与配置（不需要服务器）
local ec = assert(p4.new { prog = "p4.lua env test" })
assert(type(ec:env("PATH")) == "string")
assert(ec:env("P4LUA_DEFINITELY_UNSET_VAR") == nil)

local config_file = ec:p4config_file()
assert(config_file == nil or type(config_file) == "string")
local config_files = ec:p4config_files()
assert(type(config_files) == "table")
for i = 1, #config_files do
    assert(type(config_files[i]) == "string")
end

assert(ec:protocol("P4LUA_TEST_PROTOCOL", "on") == true)
local protocol_value = ec:protocol("P4LUA_TEST_PROTOCOL")
assert(protocol_value == nil or protocol_value == "on")
assert(ec:protocol("tag", "") == true)

assert(type(ec:is_ignored("p4-lua-not-ignored.txt")) == "boolean")

ec.cwd = "."
assert(ec.cwd == ".")
ec:close()

-- <close> 支持
do
    local close_client <close> = assert(p4.new { prog = "p4.lua close test" })
    assert(close_client:connected() == false)
end

-- Map
assert(type(p4.MAP_INCLUDE) == "number")
assert(type(p4.MAP_EXCLUDE) == "number")
assert(type(p4.MAP_OVERLAY) == "number")
assert(type(p4.MAP_ONE_TO_MANY) == "number")
assert(type(p4.MAP_LEFT_RIGHT) == "number")
assert(type(p4.MAP_RIGHT_LEFT) == "number")

local m = p4.map()
assert(m:count() == 0)
assert(m:insert("//depot/main/...", "//ws/...") == true)
assert(m:count() == 1)
assert(m:translate("//depot/main/foo") == "//ws/foo")
assert(m:translate("//ws/foo", p4.MAP_RIGHT_LEFT) == "//depot/main/foo")

m:insert("//depot/main/exclude/...", "//ws/exclude/...", p4.MAP_EXCLUDE)
assert(m:count() == 2)
assert(m:translate("//depot/main/exclude/foo") == nil)
assert(m:translate("//ws/exclude/foo", p4.MAP_RIGHT_LEFT) == nil)

local entries = m:entries()
assert(#entries == 2)
assert(entries[1].left == "//depot/main/..." and entries[1].right == "//ws/..." and entries[1].type == p4.MAP_INCLUDE)
assert(entries[2].left == "//depot/main/exclude/..." and entries[2].type == p4.MAP_EXCLUDE)

local m2 = p4.map()
m2:insert("//depot/a/...", "a/...")
m2:insert("//depot/a/...", "b/...", p4.MAP_ONE_TO_MANY)
assert(m2:translate("//depot/a/foo") == "a/foo")
assert(m2:translate("a/foo", p4.MAP_RIGHT_LEFT) == "//depot/a/foo")
local arr = m2:translate_array("//depot/a/foo")
assert(#arr == 2)
local seen = { [arr[1]] = true, [arr[2]] = true }
assert(seen["a/foo"] and seen["b/foo"], table.concat(arr, ","))

local overlay = p4.map()
overlay:insert("//depot/a/...", "a/...")
overlay:insert("//depot/a/...", "b/...", p4.MAP_OVERLAY)
assert(overlay:translate("//depot/a/foo") == "b/foo")

local reversed = p4.map()
reversed:insert("//depot/main/...", "//ws/...")
local r = reversed:reverse()
assert(r:translate("//ws/foo") == "//depot/main/foo")

local joined = p4.map()
joined:insert("//depot/...", "//client/...")
local tail = p4.map()
tail:insert("//client/...", "C:/ws/...")
joined = joined:join(tail)
assert(joined:translate("//depot/a.c") == "C:/ws/a.c")

local invalid_ok, invalid_err = pcall(function()
    m:insert("//a/...", "//b/...", 99)
end)
assert(not invalid_ok and tostring(invalid_err):find("invalid map type", 1, true))

local empty = p4.map()
assert(empty:count() == 0 and #empty:entries() == 0)

m:clear()
assert(m:count() == 0)
