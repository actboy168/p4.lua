local lm = require "luamake"
local sdkcfg = require "compile.p4sdk_config"

lm.builddir = "build/" .. lm.os

if lm.os == "macos" then
    error "macOS support is not wired yet; add the macOS P4 SDK package and platform link settings first."
end

local windows = sdkcfg.get "windows"
local linux = sdkcfg.get "linux"

lm:lua_dll "p4" {
    cxx = "c++17",
    luaversion = "lua55",
    sources = "src/*.cpp",
    windows = {
        crt = "static",
        defines = {
            "OS_NT",
            "CASE_INSENSITIVE",
        },
        includes = {
            sdkcfg.p4sdk_include(windows),
        },
        linkdirs = {
            sdkcfg.p4sdk_libdir(windows),
            windows.openssl.libdir,
        },
        links = {
            "libclient",
            "librpc",
            "libsupp",
            "libp4script",
            "libp4script_c",
            "libp4script_curl",
            "libp4script_sqlite",
            "libssl64MT",
            "libcrypto64MT",
            "Kernel32",
            "Ws2_32",
            "Advapi32",
            "Crypt32",
            "Shell32",
            "Ole32",
            "User32",
            "oldnames",
        },
    },
    linux = {
        defines = {
            "OS_LINUX",
        },
        includes = {
            sdkcfg.p4sdk_include(linux),
        },
        linkdirs = {
            sdkcfg.p4sdk_libdir(linux),
        },
        links = {
            "client",
            "rpc",
            "supp",
            "p4api",
            "p4script",
            "p4script_c",
            "p4script_curl",
            "p4script_sqlite",
            "ssl",
            "crypto",
            "rt",
        },
    },
}
