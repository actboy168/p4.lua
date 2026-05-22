local sep = package.config:sub(1, 1)
local ext = sep == "\\" and "dll" or "so"
package.cpath = table.concat({
    "build/windows/bin/?." .. ext,
    "build/linux/bin/?." .. ext,
    package.cpath,
}, ";")

local p4 = require "p4"

local client = assert(p4.new {
    port     = os.getenv("P4PORT"),
    user     = os.getenv("P4USER"),
    client   = os.getenv("P4CLIENT"),
    password = os.getenv("P4PASSWD"),
    cwd      = os.getenv("P4CWD"),
    prog     = "p4.lua example/info",
})

local result = client:run("info")
if not result.ok then
    for _, e in ipairs(result.errors) do
        io.stderr:write(("[%d] %s\n"):format(e.code, e.message))
    end
    client:close()
    return
end

for _, stat in ipairs(result.stats) do
    for k, v in pairs(stat) do
        print(k .. " = " .. v)
    end
end

client:close()
