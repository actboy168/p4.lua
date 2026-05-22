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

print("ok:", result.ok)
print("error_count:", result.error_count)

for i, entry in ipairs(result.info) do
    print(("info[%d][%d]: %s"):format(i, entry.level, entry.data))
end

for i, message in ipairs(result.errors) do
    io.stderr:write(("error[%d]: %s\n"):format(i, message))
end
