@echo off
setlocal enabledelayedexpansion

:: ═══════════════════════════════════════════════════════════
::  T850 Engine — Project Setup & Launch
::  Downloads vcpkg dependencies and opens the solution.
::
::  Usage:
::    LaunchSolution.bat              x64 only (default)
::    LaunchSolution.bat --x86        x64 + x86
::    LaunchSolution.bat --arm64      x64 + ARM64
::    LaunchSolution.bat --all        x64 + x86 + ARM64
::    LaunchSolution.bat --skip       skip vcpkg, just open solution
::    LaunchSolution.bat --setup-only install dependencies without opening solution
:: ═══════════════════════════════════════════════════════════

set "ROOT=%~dp0"
set "VCPKG_DIR=%ROOT%T850\Librerias\vcpkg"
set "VCPKG_EXE=%VCPKG_DIR%\vcpkg.exe"
set "SOLUTION=%ROOT%T850\T850.sln"

set BUILD_X86=0
set BUILD_ARM64=0
set SKIP_VCPKG=0
set SKIP_LAUNCH=0

:: Parse command-line arguments
:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="--x86"   set BUILD_X86=1
if /i "%~1"=="--arm64" set BUILD_ARM64=1
if /i "%~1"=="--all"   set BUILD_X86=1& set BUILD_ARM64=1
if /i "%~1"=="--skip"  set SKIP_VCPKG=1
if /i "%~1"=="--setup-only" set SKIP_LAUNCH=1
shift
goto parse_args
:done_args

if %SKIP_VCPKG%==1 (
    echo [T850] Skipping vcpkg setup...
    goto launch
)

:: ── Clone vcpkg if not present ──
if not exist "%VCPKG_DIR%\bootstrap-vcpkg.bat" (
    echo [T850] Cloning vcpkg...
    git clone https://github.com/microsoft/vcpkg.git "%VCPKG_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone vcpkg. Is git installed?
        pause
        exit /b 1
    )
)

:: ── Bootstrap vcpkg if executable doesn't exist ──
if not exist "%VCPKG_EXE%" (
    echo [T850] Bootstrapping vcpkg...
    call "%VCPKG_DIR%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 (
        echo [ERROR] vcpkg bootstrap failed.
        pause
        exit /b 1
    )
)

echo.
echo ════════════════════════════════════════
echo  T850 — Installing vcpkg dependencies
echo ════════════════════════════════════════

:: ── Common packages for all platforms ──
set "PACKAGES=glew vulkan-headers vulkan-loader vulkan-memory-allocator glslang draco"
set "PACKAGES_DYNAMIC=angle draco"

:: ── x64 (always) ──
echo.
echo [T850] Installing x64 packages...
for %%p in (%PACKAGES%) do (
    echo   %%p:x64-windows-static
    "%VCPKG_EXE%" install %%p:x64-windows-static --no-print-usage 2>nul
)
for %%p in (%PACKAGES_DYNAMIC%) do (
    echo   %%p:x64-windows
    "%VCPKG_EXE%" install %%p:x64-windows --no-print-usage 2>nul
)
call :install_imgui x64-windows-static dx12
if errorlevel 1 exit /b 1

:: ── x86 (optional) ──
if %BUILD_X86%==1 (
    echo.
    echo [T850] Installing x86 packages...
    for %%p in (%PACKAGES%) do (
        echo   %%p:x86-windows-static
        "%VCPKG_EXE%" install %%p:x86-windows-static --no-print-usage 2>nul
    )
    for %%p in (%PACKAGES_DYNAMIC%) do (
        echo   %%p:x86-windows
        "%VCPKG_EXE%" install %%p:x86-windows --no-print-usage 2>nul
    )
    call :install_imgui x86-windows-static nodx12
    if errorlevel 1 exit /b 1
)

:: ── ARM64 (optional) ──
if %BUILD_ARM64%==1 (
    echo.
    echo [T850] Installing ARM64 packages...
    for %%p in (%PACKAGES%) do (
        echo   %%p:arm64-windows-static
        "%VCPKG_EXE%" install %%p:arm64-windows-static --no-print-usage 2>nul
    )
    for %%p in (%PACKAGES_DYNAMIC%) do (
        echo   %%p:arm64-windows
        "%VCPKG_EXE%" install %%p:arm64-windows --no-print-usage 2>nul
    )
    call :install_imgui arm64-windows-static dx12
    if errorlevel 1 exit /b 1
)

echo.
echo [T850] vcpkg setup complete.

:launch
if %SKIP_LAUNCH%==1 (
    echo.
    echo [T850] Setup complete. Solution launch skipped.
    exit /b 0
)

echo.
echo [T850] Opening solution: %SOLUTION%
start "" "%SOLUTION%"
exit /b 0

:install_imgui
set "IMGUI_TRIPLET=%~1"
set "IMGUI_DX12_MODE=%~2"

:: Dear ImGui is required by both the editor and runtime dev panels.
:: vcpkg classic mode will not add features to an already-installed port, so
:: verify the backend headers and remove the package before reinstalling if a
:: previous core-only install is present.
set "IMGUI_INCLUDE_DIR=%VCPKG_DIR%\installed\%IMGUI_TRIPLET%\include"
set "IMGUI_FEATURES=docking-experimental,dx11-binding,vulkan-binding,opengl3-binding,sdl3-binding,win32-binding"
if /i "%IMGUI_DX12_MODE%"=="dx12" set "IMGUI_FEATURES=docking-experimental,dx11-binding,dx12-binding,vulkan-binding,opengl3-binding,sdl3-binding,win32-binding"

set "IMGUI_NEEDS_REINSTALL=0"
if exist "%IMGUI_INCLUDE_DIR%\imgui.h" (
    if not exist "%IMGUI_INCLUDE_DIR%\imgui_impl_dx11.h" set "IMGUI_NEEDS_REINSTALL=1"
    if not exist "%IMGUI_INCLUDE_DIR%\imgui_impl_vulkan.h" set "IMGUI_NEEDS_REINSTALL=1"
    if not exist "%IMGUI_INCLUDE_DIR%\imgui_impl_opengl3.h" set "IMGUI_NEEDS_REINSTALL=1"
    if not exist "%IMGUI_INCLUDE_DIR%\imgui_impl_sdl3.h" set "IMGUI_NEEDS_REINSTALL=1"
    if /i "%IMGUI_DX12_MODE%"=="dx12" if not exist "%IMGUI_INCLUDE_DIR%\imgui_impl_dx12.h" set "IMGUI_NEEDS_REINSTALL=1"
    if "!IMGUI_NEEDS_REINSTALL!"=="1" (
        echo   imgui:%IMGUI_TRIPLET% present but missing backend features -- removing for reinstall
        "%VCPKG_EXE%" remove imgui:%IMGUI_TRIPLET% --recurse 2>nul
    )
)

echo   imgui:%IMGUI_TRIPLET% (runtime/editor)
"%VCPKG_EXE%" install "imgui[%IMGUI_FEATURES%]:%IMGUI_TRIPLET%" --recurse --no-print-usage
if errorlevel 1 exit /b 1

echo   imguizmo:%IMGUI_TRIPLET% (editor)
"%VCPKG_EXE%" install imguizmo:%IMGUI_TRIPLET% --recurse --no-print-usage
if errorlevel 1 exit /b 1
exit /b 0
