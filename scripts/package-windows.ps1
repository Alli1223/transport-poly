param(
    [string]$BuildDir = 'build/windows-msvc',
    [string]$Configuration = 'RelWithDebInfo',
    [string]$Iscc,
    [string]$CMakePath = 'cmake',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $repo $BuildDir }
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$cache = Get-Content -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Raw
if ($CMakePath -eq 'cmake' -and $cache -match '(?m)^CMAKE_COMMAND:INTERNAL=(.+)$') {
    $CMakePath = $Matches[1].Trim()
}
if ($cache -notmatch '(?m)^TP_UPDATE_FEED:STRING=https://api.github.com/repos/Alli1223/transport-poly/releases/latest\s*$') {
    throw 'Configure with -DTP_UPDATE_FEED=https://api.github.com/repos/Alli1223/transport-poly/releases/latest before packaging.'
}
if (-not $Iscc) {
    $candidates = @(
        (Join-Path $repo 'build/tools/InnoSetup/ISCC.exe'),
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    $Iscc = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $Iscc) { $Iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source }
}
if (-not $Iscc) { throw 'Install Inno Setup 6 or 7, or supply -Iscc with the path to ISCC.exe.' }
if (-not $SkipBuild) {
    & $CMakePath --build $BuildDir --config $Configuration
    if ($LASTEXITCODE) { throw 'Game build failed.' }
}
$source = Get-Content -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Raw
if ($source -notmatch '(?s)project\(transport_poly\s+VERSION\s+(\d+\.\d+\.\d+)') { throw 'Cannot read the package version.' }
$version = $Matches[1]
$stage = Join-Path $BuildDir ('installer-stage-' + [Guid]::NewGuid().ToString('N'))
$packages = Join-Path $BuildDir 'packages'
New-Item -ItemType Directory -Path $stage, $packages -Force | Out-Null
& $CMakePath --install $BuildDir --config $Configuration --prefix $stage
if ($LASTEXITCODE) { throw 'Staging the installation failed.' }
& $Iscc "/DAppVersion=$version" "/DSourceDir=$stage" "/DOutputDir=$packages" (Join-Path $repo 'installer/transport-poly.iss')
if ($LASTEXITCODE) { throw 'Building the Windows installer failed.' }
$cpack = Join-Path (Split-Path $CMakePath -Parent) 'cpack.exe'
& $cpack --config (Join-Path $BuildDir 'CPackConfig.cmake') -C $Configuration -B $packages
if ($LASTEXITCODE) { throw 'Building the portable ZIP failed.' }
$files = @("transport-poly-$version-windows-x64-setup.exe", "transport-poly-$version-windows-x64.zip")
$sums = foreach ($file in $files) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $packages $file) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $file"
}
[IO.File]::WriteAllLines((Join-Path $packages 'SHA256SUMS.txt'), $sums, [Text.UTF8Encoding]::new($false))
Write-Output "Installer, portable ZIP and checksums: $packages"
