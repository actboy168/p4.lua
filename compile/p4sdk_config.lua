local function join(...)
    return table.concat({ ... }, "/")
end

local cfg = {
    unsupported_platform_message = "unsupported platform: %s (macOS support needs its SDK package URL first)",
    windows = {
        p4sdk = {
            url = "https://filehost.perforce.com/perforce/r25.2/bin.ntx64/p4api_vs2022_static_openssl1.1.1.zip",
            archive = "deps/p4api_vs2022_static_openssl1.1.1.zip",
            root = "deps/p4api_vs2022_static_openssl1.1.1",
            extracted = "p4api-2025.2.2907753-vs2022_static",
            min_size = 1024 * 1024,
        },
        openssl = {
            url = "https://slproweb.com/download/Win64OpenSSL-1_1_1w.exe",
            archive = "deps/Win64OpenSSL-1_1_1w.exe",
            root = "deps/openssl-full-1.1.1w",
            min_size = 60 * 1024 * 1024,
            libdir = "deps/openssl-full-1.1.1w/lib/VC/static",
            sentinel = "deps/openssl-full-1.1.1w/lib/VC/static/libssl64MT.lib",
        },
    },
    linux = {
        p4sdk = {
            url = "https://filehost.perforce.com/perforce/r25.2/bin.linux26x86_64/p4api-glibc2.12-openssl3.tgz",
            archive = "deps/p4api-glibc2.12-openssl3.tgz",
            root = "deps/p4api-glibc2.12-openssl3",
            extracted = "p4api-2025.2.2907753",
            min_size = 1024 * 1024,
        },
    },
}

function cfg.get(os)
    local platform = cfg[os]
    assert(platform, (cfg.unsupported_platform_message):format(os))
    return platform
end

function cfg.p4sdk_include(platform)
    return join(platform.p4sdk.root, platform.p4sdk.extracted, "include")
end

function cfg.p4sdk_libdir(platform)
    return join(platform.p4sdk.root, platform.p4sdk.extracted, "lib")
end

function cfg.p4sdk_sentinel(platform)
    return join(platform.p4sdk.root, platform.p4sdk.extracted, "include", "p4", "clientapi.h")
end

return cfg
