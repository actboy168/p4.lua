local platform = require "bee.platform"
local fs = require "bee.filesystem"
local sp = require "bee.subprocess"
local sdkcfg = require "compile.p4sdk_config"

local powershell = ((os.getenv "WINDIR") or "C:\\Windows") .. "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"
local curl = ((os.getenv "WINDIR") or "C:\\Windows") .. "\\System32\\curl.exe"

local cfg = sdkcfg.get(platform.os)

local p4sdk_url = cfg.p4sdk.url
local p4sdk_archive = cfg.p4sdk.archive
local p4sdk_root = cfg.p4sdk.root
local p4sdk_sentinel = fs.path(sdkcfg.p4sdk_sentinel(cfg))
local p4sdk_archive_min_size = cfg.p4sdk.min_size

local openssl = cfg.openssl
local openssl_url = openssl and openssl.url
local openssl_installer = openssl and openssl.archive
local openssl_root = openssl and openssl.root
local openssl_sentinel = openssl and fs.path(openssl.sentinel)
local openssl_installer_min_size = openssl and openssl.min_size

local function run(cmd)
    local p, err = sp.spawn(cmd, {
        stdin = false,
        stdout = "inherit",
        stderr = "inherit",
    })
    if not p then
        error(cmd[1] .. ":" .. err)
    end
    local ok, _, code = p:wait()
    if not ok then
        error(cmd[1] .. " failed with exit code " .. tostring(code))
    end
end

local function file_size(path)
    local f = io.open(path, "rb")
    if not f then
        return nil
    end
    local size = f:seek("end")
    f:close()
    return size
end

local function ensure_valid_download(path, min_size)
    if not fs.exists(path) then
        return false
    end
    local size = file_size(path)
    if not size or size < min_size then
        fs.remove(path)
        return false
    end
    return true
end

local function download(url, output)
    if platform.os == "windows" then
        run {
            curl,
            "-L",
            "-f",  -- HTTP 错误（如 404）时失败退出，不把错误页写入文件
            url,
            "-o",
            output,
        }
        return
    end

    local downloader = platform.os == "macos" and { "curl", "--location", "--fail", "-C", "-" } or "wget"
    local cmd = {
        downloader,
        url,
        downloader == "wget" and "-O" or "-o",
        output,
    }
    run(cmd)
end

local function extract_archive(archive, dir)
    if platform.os == "windows" then
        run {
            powershell,
            "-NoProfile",
            "-Command",
            ("Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force"):format(archive, dir),
        }
        return
    end

    run { "tar", "-xvf", archive, "-C", dir }
end

local function install_openssl(installer, dir)
    if platform.os ~= "windows" then
        error("OpenSSL installer script currently only supports windows")
    end
    run {
        installer,
        "/VERYSILENT",
        "/SP-",
        "/SUPPRESSMSGBOXES",
        "/NORESTART",
        "/CURRENTUSER",
        "/DIR=" .. dir,
    }
end

fs.create_directory "deps"

if not fs.exists(p4sdk_sentinel) then
    if not ensure_valid_download(p4sdk_archive, p4sdk_archive_min_size) then
        print("[1/2] download ", p4sdk_url)
        download(p4sdk_url, p4sdk_archive)
    else
        print("use cached ", p4sdk_archive)
    end
    fs.remove_all(p4sdk_root)
    fs.create_directories(p4sdk_root)
    print("[1/2] extract  ", p4sdk_archive)
    extract_archive(p4sdk_archive, p4sdk_root)
    -- flatten if archive has a single top-level directory
    local dirs = {}
    for entry in fs.pairs(p4sdk_root) do
        if fs.is_directory(entry) then
            dirs[#dirs + 1] = entry
        else
            dirs = {}
            break
        end
    end
    if #dirs == 1 then
        for entry in fs.pairs(dirs[1]) do
            fs.rename(entry, p4sdk_root / entry:filename())
        end
        fs.remove_all(dirs[1])
    end
else
    print("use cached ", p4sdk_sentinel)
end

if openssl and not fs.exists(openssl_sentinel) then
    if not ensure_valid_download(openssl_installer, openssl_installer_min_size) then
        print("[2/2] download ", openssl_url)
        download(openssl_url, openssl_installer)
    else
        print("use cached ", openssl_installer)
    end
    fs.remove_all(openssl_root)
    fs.create_directories(openssl_root)
    print("[2/2] install  ", openssl_root)
    install_openssl(openssl_installer, openssl_root)
elseif openssl then
    print("use cached ", openssl_sentinel)
end

assert(fs.exists(p4sdk_sentinel), "P4 SDK is missing after download")
if openssl then
    assert(fs.exists(openssl_sentinel), "OpenSSL is missing after install")
end
