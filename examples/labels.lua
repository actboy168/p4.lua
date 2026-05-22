-- List labels
-- Usage: luamake lua examples/labels.lua

local sep = package.config:sub(1, 1)
local ext = sep == "\\" and "dll" or "so"
package.cpath = table.concat({
    "build/windows/bin/?." .. ext,
    "build/linux/bin/?." .. ext,
    package.cpath,
}, ";")

local p4 = require "p4"

local client, err = p4.new { prog = "p4.lua example/labels" }
if not client then
    io.stderr:write(("p4.new failed: %s\n"):format(err))
    return
end

local args = {}
local labels = client:run("labels", args)
if not labels.ok then
    io.stderr:write("labels query failed\n")
    client:close()
    return
end

for _, lb in ipairs(labels.stats) do
    local desc = (lb.Description or ""):gsub("\n$", "")
    print(("  %-25s %s"):format(lb.label, desc))
end

print(("\nTotal: %d labels"):format(#labels.stats))
client:close()
