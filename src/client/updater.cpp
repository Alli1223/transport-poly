#include "client/updater.hpp"

#include "core/log.hpp"
#include "core/sha256.hpp"

#include <cstdio>
#include <optional>
#include <cstdlib>
#include <format>
#include <fstream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#else
#include <unistd.h>
#endif

namespace tp::update {

namespace {

std::filesystem::path downloadDir() {
    return std::filesystem::temp_directory_path() / "transport-poly-update" /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

// Where a portable or Linux copy of the game is unpacked: the folder holding the executable (or the
// folder above "bin" on Linux).
std::filesystem::path installRoot() {
    const std::filesystem::path dir = executablePath().parent_path();
    return dir.filename() == "bin" ? dir.parent_path() : dir;
}

#ifdef _WIN32
// Starts a program that outlives the game: out of any job the game runs in (some launchers and
// shells close every process of the job when the game exits), when the job allows that.
bool launchDetached(std::wstring command, DWORD flags) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &pi);
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED)
        ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
#endif

bool writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

} // namespace

#ifdef _WIN32

std::filesystem::path executablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) return {};
        if (n < buffer.size()) {
            buffer.resize(n);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

releases::Kind installKind() {
    std::error_code ec;
    return std::filesystem::exists(executablePath().parent_path() / "unins000.exe", ec) ? releases::Kind::WindowsInstaller
                                                                                       : releases::Kind::WindowsPortable;
}

std::filesystem::path userDataDir() {
    if (const wchar_t* appData = _wgetenv(L"APPDATA"); appData && *appData) return std::filesystem::path(appData) / "transport-poly";
    return executablePath().parent_path();
}

bool openUrl(const std::string& url) {
    const std::wstring wide(url.begin(), url.end());
    return reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool httpGet(const std::string& url, std::vector<u8>& out, std::string& error, std::atomic<u64>* got, bool json) {
    struct Handle {
        HINTERNET h = nullptr;
        ~Handle() {
            if (h) WinHttpCloseHandle(h);
        }
    };
    const std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {}, path[4096] = {}, extra[4096] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 4096;
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) {
        error = "Bad address: " + url;
        return false;
    }
    Handle session{WinHttpOpen(L"transport-poly-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) session.h = WinHttpOpen(L"transport-poly-updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session.h) WinHttpSetTimeouts(session.h, 5000, 5000, 5000, json ? 5000 : 15000);
    Handle connection{session.h ? WinHttpConnect(session.h, host, parts.nPort, 0) : nullptr};
    const std::wstring object = std::wstring(path) + extra;
    Handle request{connection.h ? WinHttpOpenRequest(connection.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                : nullptr};
    const wchar_t* headers = json ? L"Accept: application/vnd.github+json\r\n" : L"Accept: application/octet-stream\r\n";
    if (!request.h || !WinHttpSendRequest(request.h, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        std::string name;
        for (const wchar_t* c = host; *c; ++c) name += static_cast<char>(*c < 128 ? *c : '?');
        error = std::format("Could not reach {} (error {})", name, GetLastError());
        return false;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        error = status == 404 ? std::string("No release found (the releases may not be public)") : std::format("The server answered {}", status);
        return false;
    }
    out.clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) {
            error = "The download was interrupted";
            return false;
        }
        if (available == 0) break;
        const usize at = out.size();
        if (at + available > (json ? 16u * 1024u * 1024u : 1024u * 1024u * 1024u)) {
            error = "The update response is too large";
            return false;
        }
        out.resize(at + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, out.data() + at, available, &read)) {
            error = "The download was interrupted";
            return false;
        }
        out.resize(at + read);
        if (got) *got += read;
    }
    return true;
}

#else

std::filesystem::path executablePath() {
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec);
}

releases::Kind installKind() { return releases::Kind::Linux; }

std::filesystem::path userDataDir() {
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) return std::filesystem::path(data) / "transport-poly";
    if (const char* home = std::getenv("HOME"); home && *home) return std::filesystem::path(home) / ".local/share/transport-poly";
    return executablePath().parent_path();
}

bool openUrl(const std::string& url) {
    if (url.find('\'') != std::string::npos) return false;
    return std::system(std::format("xdg-open '{}' >/dev/null 2>&1 &", url).c_str()) == 0;
}

bool httpGet(const std::string& url, std::vector<u8>& out, std::string& error, std::atomic<u64>* got, bool json) {
    if (url.find('\'') != std::string::npos) {
        error = "Bad address";
        return false;
    }
    const std::string command = std::format("curl -fsSL --max-time 900 -A transport-poly-updater -H 'Accept: {}' '{}'",
                                            json ? "application/vnd.github+json" : "application/octet-stream", url);
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        error = "Could not run curl";
        return false;
    }
    out.clear();
    u8 buffer[65536];
    for (usize n; (n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0;) {
        out.insert(out.end(), buffer, buffer + n);
        if (got) *got += n;
    }
    if (pclose(pipe) != 0) {
        error = "The download failed (is curl installed, and are the releases public?)";
        return false;
    }
    return true;
}

#endif

Updater::~Updater() { join(); }

void Updater::join() {
    if (worker_.joinable()) worker_.join();
}

void Updater::fail(const std::string& why) {
    std::scoped_lock lock(mutex_);
    state_ = State::Failed;
    error_ = why;
    log::warn("Update: {}", why);
}

Updater::State Updater::state() const {
    std::scoped_lock lock(mutex_);
    return state_;
}

std::string Updater::error() const {
    std::scoped_lock lock(mutex_);
    return error_;
}

releases::Release Updater::release() const {
    std::scoped_lock lock(mutex_);
    return release_;
}

void Updater::check(const std::string& feedUrl, const std::string& currentVersion) {
    join();
    {
        std::scoped_lock lock(mutex_);
        state_ = State::Checking;
        error_.clear();
        release_ = {};
    }
    worker_ = std::thread([this, feedUrl, currentVersion] {
      try {
        std::vector<u8> body;
        std::string err;
        if (!httpGet(feedUrl, body, err, nullptr, true)) return fail(err);
        const auto release = releases::parseRelease(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()));
        if (!release) return fail("The release feed could not be read");
        std::scoped_lock lock(mutex_);
        release_ = *release;
        state_ = releases::compareVersions(release->version, currentVersion) > 0 ? State::Available : State::UpToDate;
        log::info("Update: latest release {}, this is {}", release->version, currentVersion);
      } catch (const std::exception& e) { fail(std::string("Could not check for updates: ") + e.what()); }
    });
}

void Updater::download() {
    join();
    const releases::Release release = this->release();
    const releases::Asset* asset = releases::pickAsset(release, installKind());
    if (!asset) return fail("This release has no download for this kind of installation");
    const releases::Asset* sums = releases::checksums(release);
    if (!sums) return fail("This release has no SHA256SUMS.txt; open the release page or try again later");
    {
        std::scoped_lock lock(mutex_);
        state_ = State::Downloading;
        error_.clear();
    }
    got_ = 0;
    total_ = asset->size;
    worker_ = std::thread([this, asset = *asset, sums = *sums] {
      try {
        std::vector<u8> data;
        std::string err;
        if (!httpGet(asset.url, data, err, &got_)) return fail(err);
        // The download must match the checksum published with the release.
        {
            std::vector<u8> listing;
            if (!httpGet(sums.url, listing, err, nullptr, true)) return fail(err);
            const auto expected = releases::checksumFor(std::string_view(reinterpret_cast<const char*>(listing.data()), listing.size()), asset.name);
            if (!expected) return fail("The release does not list a checksum for " + asset.name);
            if (sha256Hex(data) != *expected) return fail("The download is damaged (its checksum does not match); try again");
        }
        std::error_code ec;
        const auto folder = downloadDir();
        std::filesystem::create_directories(folder, ec);
        if (ec) return fail("Could not create the update download folder");
        const std::filesystem::path file = folder / asset.name;
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        out.close();
        if (!out) return fail("Could not save the update to " + file.string());
        std::scoped_lock lock(mutex_);
        file_ = file;
        state_ = State::Ready;
        log::info("Update: downloaded {} ({} bytes)", asset.name, data.size());
      } catch (const std::exception& e) { fail(std::string("Could not download the update: ") + e.what()); }
    });
}

bool Updater::apply() {
    join();
    std::filesystem::path file;
    {
        std::scoped_lock lock(mutex_);
        if (state_ != State::Ready) return false;
        file = file_;
    }
    const releases::Kind kind = installKind();
    const std::filesystem::path exe = executablePath();
    const std::filesystem::path root = installRoot();
#ifdef _WIN32
    if (kind == releases::Kind::WindowsInstaller) {
        // The installer upgrades the installation in place (it remembers where it is), closes the game
        // if it is still running, and starts it again when done. An installation for every user needs
        // administrator rights, which only the shell can ask for.
        wchar_t scope[32]{};
        GetPrivateProfileStringW(L"Install", L"Scope", L"currentuser", scope, 32, (root / "install-mode.ini").c_str());
        const bool allUsers = std::wstring_view(scope) == L"allusers";
        const std::wstring arguments = std::format(L"/SP- /SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /RESTARTGAME=1 /{} /DIR=\"{}\"",
                                                   allUsers ? L"ALLUSERS" : L"CURRENTUSER", root.wstring());
        // Let Inno Setup request elevation itself, retaining the original user's token
        // so the restarted game does not inherit administrator privileges.
        if (launchDetached(std::format(L"\"{}\" {}", file.wstring(), arguments), 0)) return true;
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", file.wstring().c_str(), arguments.c_str(), nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<intptr_t>(r) <= 32) {
            fail("Could not start the installer");
            return false;
        }
        return true;
    }
    // Portable: once the game has quit, unpack the zip over it and start the new version.
    const std::filesystem::path script = file.parent_path() / "update.ps1";
    const std::string text = R"(param([int]$ProcessId, [string]$Archive, [string]$Target, [string]$Exe)
$ErrorActionPreference = 'Stop'
$work = Join-Path $PSScriptRoot ('files-' + [Guid]::NewGuid().ToString('N'))
$unpacked = Join-Path $work 'new'
$backup = Join-Path $work 'backup'
$changed = [Collections.Generic.List[string]]::new()
$created = [Collections.Generic.List[string]]::new()
try {
    $running = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($running) { $running | Wait-Process -Timeout 120 -ErrorAction Stop }
    $Target = (Resolve-Path -LiteralPath $Target).Path
    $prefix = $Target.TrimEnd('\') + '\'
    Expand-Archive -LiteralPath $Archive -DestinationPath $unpacked
    $source = $unpacked
    if (-not (Test-Path -LiteralPath (Join-Path $source 'transport-poly.exe'))) {
        $top = @(Get-ChildItem -LiteralPath $unpacked -Directory)
        if ($top.Count -ne 1) { throw 'The update has an unexpected folder layout.' }
        $source = $top[0].FullName
    }
    if (-not (Test-Path -LiteralPath (Join-Path $source 'transport-poly.exe'))) { throw 'The update does not contain the game.' }
    foreach ($item in @(Get-ChildItem -LiteralPath $source -File -Recurse)) {
        $relative = $item.FullName.Substring($source.Length + 1)
        if ($relative -match '^(saves|screenshots)(\\|$)|^(settings.cfg|imgui.ini|install-mode.ini|unins.*)$') { continue }
        $dest = [IO.Path]::GetFullPath((Join-Path $Target $relative))
        if (-not $dest.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'The update contains an invalid path.' }
        if (Test-Path -LiteralPath $dest) {
            $old = Join-Path $backup $relative
            New-Item -ItemType Directory -Path (Split-Path $old -Parent) -Force | Out-Null
            Copy-Item -LiteralPath $dest -Destination $old
            $changed.Add($relative)
        } else { $created.Add($relative) }
        New-Item -ItemType Directory -Path (Split-Path $dest -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $item.FullName -Destination $dest -Force
    }
    Start-Process -FilePath $Exe -WorkingDirectory $Target
    $resolved = (Resolve-Path -LiteralPath $work).Path
    $allowed = (Resolve-Path -LiteralPath $PSScriptRoot).Path.TrimEnd('\') + '\'
    if ($resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
} catch {
    $problem = $_.Exception.Message
    foreach ($relative in $changed) {
        Copy-Item -LiteralPath (Join-Path $backup $relative) -Destination (Join-Path $Target $relative) -Force -ErrorAction Continue
    }
    foreach ($relative in $created) { Remove-Item -LiteralPath (Join-Path $Target $relative) -Force -ErrorAction Continue }
    $message = "The update failed. Your previous files were restored. Details: $problem"
    $message | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'update-error.txt')
    Add-Type -AssemblyName PresentationFramework
    [Windows.MessageBox]::Show($message, 'transport-poly update') | Out-Null
    exit 1
}
)";
    if (!writeFile(script, text)) {
        fail("Could not write the update script");
        return false;
    }
    std::wstring command = std::format(L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"{}\" -ProcessId {} "
                                       L"-Archive \"{}\" -Target \"{}\" -Exe \"{}\"",
                                       script.wstring(), GetCurrentProcessId(), file.wstring(), root.wstring(), exe.wstring());
    if (!launchDetached(std::move(command), CREATE_NO_WINDOW)) {
        fail("Could not start the update");
        return false;
    }
    return true;
#else
    (void)kind;
    // Once the game has quit, unpack the tarball over it and start the new version.
    const std::filesystem::path script = file.parent_path() / "update.sh";
    const std::string text = R"(#!/bin/sh
pid="$1"; archive="$2"; target="$3"; exe="$4"
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
unpacked=$(mktemp -d) || exit 1
tar -xzf "$archive" -C "$unpacked" || exit 1
top=$(find "$unpacked" -mindepth 1 -maxdepth 1 -type d | head -n 1)
[ -n "$top" ] && [ -d "$top/bin" ] || top="$unpacked"
cp -rf "$top"/. "$target"/ || exit 1
rm -rf "$unpacked"
cd "$target" && "$exe" >/dev/null 2>&1 &
)";
    for (const auto& p : {file, root, exe, script})
        if (p.string().find('\'') != std::string::npos) {
            fail("The game's folder name has a quote in it; update by hand");
            return false;
        }
    if (!writeFile(script, text)) {
        fail("Could not write the update script");
        return false;
    }
    const std::string command = std::format("setsid sh '{}' {} '{}' '{}' '{}' >/dev/null 2>&1 &", script.string(), getpid(), file.string(),
                                            root.string(), exe.string());
    if (std::system(command.c_str()) != 0) {
        fail("Could not start the update");
        return false;
    }
    return true;
#endif
}

} // namespace tp::update
