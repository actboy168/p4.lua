local cfg = {
    windows = {
        p4sdk   = "https://filehost.perforce.com/perforce/r26.1/bin.ntx64/p4api_vs2022_static_openssl3.5.zip",
        openssl = "https://slproweb.com/download/Win64OpenSSL-3_5_7.exe",
    },
    linux = {
        p4sdk = "https://filehost.perforce.com/perforce/r26.1/bin.linux26x86_64/p4api-glibc2.3-openssl3.5.tgz",
    },
}

local function join(...)
    return table.concat({ ... }, "/")
end

local MIN_FILE_SIZE = 1024 * 1024

local function derive_p4sdk(url)
    if not url then
        return
    end
    local filename = url:match("/([^/]+)$")
    local archive = "deps/" .. filename
    return { url = url, archive = archive, root = archive:gsub("%.[^.]+$", ""), min_size = MIN_FILE_SIZE }
end

local function derive_openssl(url)
    if not url then
        return
    end
    local filename = url:match("/([^/]+)$")
    local archive = "deps/" .. filename
    local root = archive:gsub("%.[^.]+$", "")
    local libdir = root .. "/lib/VC/x64/MT"
    return {
        url = url,
        archive = archive,
        root = root,
        min_size = MIN_FILE_SIZE,
        libdir = libdir,
        sentinel = libdir .. "/libssl.lib",
    }
end

function cfg.get(os)
    local platform = cfg[os]
    assert(platform, "unsupported platform: " .. os)
    return {
        p4sdk = derive_p4sdk(platform.p4sdk),
        openssl = derive_openssl(platform.openssl),
    }
end

function cfg.p4sdk_include(platform)
    return join(platform.p4sdk.root, "include")
end

function cfg.p4sdk_libdir(platform)
    return join(platform.p4sdk.root, "lib")
end

function cfg.p4sdk_sentinel(platform)
    return join(platform.p4sdk.root, "include", "p4", "clientapi.h")
end

return cfg
