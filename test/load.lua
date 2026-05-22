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

client:close()
