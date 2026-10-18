@echo off
rem Generates the Visual Studio 2026 projects with CMake and opens transport-poly.slnx.
setlocal
cd /d "%~dp0.."

set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 (
    rem Fall back to the CMake that ships with Visual Studio.
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -property installationPath`) do (
        set "CMAKE=%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
)

"%CMAKE%" --preset windows-vs2026
if errorlevel 1 (
    echo CMake configuration failed.
    exit /b 1
)
start "" "transport-poly.slnx"
