@echo off
setlocal enabledelayedexpansion

:: T850 Engine — Android Toolchain Setup for Windows
:: Installs the Android command-line toolchain used by the NativeActivity Vulkan backend.
:: Usage:
::   SetupAndroidToolchain.bat
::   SetupAndroidToolchain.bat --sdk C:\Android\Sdk
::   SetupAndroidToolchain.bat --skip-winget

set "ROOT=%~dp0"
set "ANDROID_SDK=%LOCALAPPDATA%\Android\Sdk"
set "CMDLINE_VERSION=11076708"
set "ANDROID_PLATFORM=android-35"
set "BUILD_TOOLS=35.0.0"
set "NDK_VERSION=27.2.12479018"
set "CMAKE_VERSION=3.22.1"
set "SKIP_WINGET=0"

:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="--sdk" (
    shift
    if "%~1"=="" goto usage
    set "ANDROID_SDK=%~1"
) else if /i "%~1"=="--skip-winget" (
    set "SKIP_WINGET=1"
) else (
    goto usage
)
shift
goto parse_args
:done_args

echo.
echo ========================================
echo  T850 — Android Toolchain Setup
echo ========================================
echo SDK root: %ANDROID_SDK%
echo.

where powershell >nul 2>nul || (
    echo [ERROR] PowerShell is required.
    exit /b 1
)

where git >nul 2>nul || (
    echo [WARN] Git was not found on PATH. Install Git before building T850 Android packages.
)

if "%SKIP_WINGET%"=="0" (
    where winget >nul 2>nul
    if not errorlevel 1 (
        echo [T850] Installing JDK 17, Ninja, and Vulkan SDK with winget when missing...
        winget install --id EclipseAdoptium.Temurin.17.JDK -e --accept-package-agreements --accept-source-agreements
        winget install --id Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements
        winget install --id KhronosGroup.VulkanSDK -e --accept-package-agreements --accept-source-agreements
    ) else (
        echo [WARN] winget not found. Please install JDK 17, Ninja, and Vulkan SDK manually.
    )
) else (
    echo [T850] Skipping winget package installation.
)

if not defined JAVA_HOME (
    for /d %%j in ("%ProgramFiles%\Eclipse Adoptium\jdk-17*" "%ProgramFiles%\Java\jdk-17*") do (
        if exist "%%~fj\bin\java.exe" set "JAVA_HOME=%%~fj"
    )
)
if not defined JAVA_HOME (
    echo [WARN] JAVA_HOME is not set. Set it to a JDK 17+ install before building.
) else (
    set "PATH=%JAVA_HOME%\bin;%PATH%"
)

set "ANDROID_HOME=%ANDROID_SDK%"
set "ANDROID_SDK_ROOT=%ANDROID_SDK%"
set "CMDLINE_DIR=%ANDROID_SDK%\cmdline-tools\latest"
set "SDKMANAGER=%CMDLINE_DIR%\bin\sdkmanager.bat"

if not exist "%SDKMANAGER%" (
    echo [T850] Downloading Android command-line tools...
    mkdir "%ANDROID_SDK%\cmdline-tools" 2>nul
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
      "$ErrorActionPreference='Stop'; $zip='%TEMP%\android-commandlinetools.zip'; Invoke-WebRequest -Uri 'https://dl.google.com/android/repository/commandlinetools-win-%CMDLINE_VERSION%_latest.zip' -OutFile $zip; $tmp=Join-Path $env:TEMP 't850-android-cmdline'; if(Test-Path $tmp){Remove-Item $tmp -Recurse -Force}; Expand-Archive $zip -DestinationPath $tmp -Force; if(Test-Path '%CMDLINE_DIR%'){Remove-Item '%CMDLINE_DIR%' -Recurse -Force}; New-Item -ItemType Directory -Force -Path '%CMDLINE_DIR%' | Out-Null; Move-Item (Join-Path $tmp 'cmdline-tools\*') '%CMDLINE_DIR%'"
    if errorlevel 1 (
        echo [ERROR] Failed to download Android command-line tools.
        exit /b 1
    )
)

echo [T850] Accepting Android SDK licenses...
powershell -NoProfile -ExecutionPolicy Bypass -Command "1..20 | ForEach-Object { 'y' }" | "%SDKMANAGER%" --sdk_root="%ANDROID_SDK%" --licenses
if errorlevel 1 echo [WARN] License acceptance reported a non-zero exit code. Re-run this script if installs fail.

echo [T850] Installing Android SDK packages...
"%SDKMANAGER%" --sdk_root="%ANDROID_SDK%" ^
    "platform-tools" ^
    "platforms;%ANDROID_PLATFORM%" ^
    "build-tools;%BUILD_TOOLS%" ^
    "ndk;%NDK_VERSION%" ^
    "cmake;%CMAKE_VERSION%"
if errorlevel 1 (
    echo [ERROR] Android SDK package installation failed.
    exit /b 1
)

set "ANDROID_NDK_HOME=%ANDROID_SDK%\ndk\%NDK_VERSION%"

set "VMA_INCLUDE=%ROOT%T850\Librerias\VulkanMemoryAllocator\include\vma"
if not exist "%VMA_INCLUDE%\vk_mem_alloc.h" (
    echo [T850] Downloading Vulkan Memory Allocator header...
    mkdir "%VMA_INCLUDE%" 2^>nul
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
      "$ErrorActionPreference='Stop'; Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.1.0/include/vk_mem_alloc.h' -OutFile '%VMA_INCLUDE%\vk_mem_alloc.h'"
    if errorlevel 1 (
        echo [ERROR] Failed to download Vulkan Memory Allocator.
        exit /b 1
    )
)

echo.
echo [T850] Android setup complete.
echo.
echo Add these environment variables permanently if they are not already set:
echo   setx JAVA_HOME "%JAVA_HOME%"
echo   setx ANDROID_HOME "%ANDROID_HOME%"
echo   setx ANDROID_SDK_ROOT "%ANDROID_SDK_ROOT%"
echo   setx ANDROID_NDK_HOME "%ANDROID_NDK_HOME%"
echo.
echo Current session values:
echo   JAVA_HOME=%JAVA_HOME%
echo   ANDROID_HOME=%ANDROID_HOME%
echo   ANDROID_SDK_ROOT=%ANDROID_SDK_ROOT%
echo   ANDROID_NDK_HOME=%ANDROID_NDK_HOME%
echo.
echo Build Android package from: %ROOT%T850\android
echo.
exit /b 0

:usage
echo Usage: SetupAndroidToolchain.bat [--sdk C:\Android\Sdk] [--skip-winget]
exit /b 1
