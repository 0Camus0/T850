@echo off
setlocal enabledelayedexpansion

:: T850 Engine - Android Toolchain Setup for Windows
:: Installs the Android command-line toolchain used by the NativeActivity Vulkan backend.
:: Usage:
::   SetupAndroidToolchain.bat
::   SetupAndroidToolchain.bat --sdk C:\Android\Sdk
::   SetupAndroidToolchain.bat --skip-winget
::   SetupAndroidToolchain.bat --with-emulator
::   SetupAndroidToolchain.bat --android-abis arm64-v8a,x86_64

set "ROOT=%~dp0"
set "ANDROID_SDK=%LOCALAPPDATA%\Android\Sdk"
set "CMDLINE_VERSION=14742923"
set "ANDROID_PLATFORM=android-35"
set "ANDROID_MIN_PLATFORM=android-28"
set "BUILD_TOOLS=35.0.0"
set "NDK_VERSION=27.2.12479018"
set "CMAKE_VERSION=3.22.1"
set "SKIP_WINGET=0"
set "WITH_EMULATOR=0"
set "EMULATOR_IMAGE=system-images;android-35;google_apis;x86_64"
set "ANDROID_ABIS=arm64-v8a"

:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="--sdk" goto arg_sdk
if /i "%~1"=="--skip-winget" goto arg_skip_winget
if /i "%~1"=="--with-emulator" goto arg_with_emulator
if /i "%~1"=="--android-abis" goto arg_android_abis
if /i "%~1"=="--abis" goto arg_android_abis
goto usage

:arg_sdk
shift
if "%~1"=="" goto usage
set "ANDROID_SDK=%~1"
shift
goto parse_args

:arg_skip_winget
set "SKIP_WINGET=1"
shift
goto parse_args

:arg_with_emulator
set "WITH_EMULATOR=1"
shift
goto parse_args

:arg_android_abis
shift
if "%~1"=="" goto usage
set "ANDROID_ABIS=%~1"
shift
goto parse_args

:done_args

if "%WITH_EMULATOR%"=="1" (
    echo,%ANDROID_ABIS%| findstr /I /C:"x86_64" >nul || set "ANDROID_ABIS=%ANDROID_ABIS%,x86_64"
)
set "ANDROID_ABIS=%ANDROID_ABIS: =%"
for %%a in ("%ANDROID_ABIS:,=" "%") do (
    if /I not "%%~a"=="arm64-v8a" if /I not "%%~a"=="x86_64" (
        echo [ERROR] Unsupported Android ABI: %%~a
        echo [ERROR] Supported ABIs: arm64-v8a,x86_64
        exit /b 1
    )
)

echo.
echo ========================================
echo  T850 - Android Toolchain Setup
echo ========================================
echo SDK root: %ANDROID_SDK%
echo Android ABIs: %ANDROID_ABIS%
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
    if exist "%ProgramFiles%\Android\Android Studio\jbr\bin\java.exe" set "JAVA_HOME=%ProgramFiles%\Android\Android Studio\jbr"
)
if not defined JAVA_HOME (
    if exist "%ProgramFiles(x86)%\Android\Android Studio\jbr\bin\java.exe" set "JAVA_HOME=%ProgramFiles(x86)%\Android\Android Studio\jbr"
)
if not defined JAVA_HOME (
    for /d %%j in ("%ProgramFiles%\Eclipse Adoptium\jdk-17*" "%ProgramFiles%\Java\jdk-17*") do (
        if exist "%%~fj\bin\java.exe" set "JAVA_HOME=%%~fj"
    )
)
if defined JAVA_HOME (
    set "PATH=%JAVA_HOME%\bin;%PATH%"
)
where java >nul 2>nul || (
    echo [ERROR] JDK 17+ is required for Android sdkmanager.
    echo [ERROR] Install JDK 17+ or rerun without --skip-winget to let winget install it.
    exit /b 1
)
if not defined JAVA_HOME (
    echo [WARN] JAVA_HOME is not set. sdkmanager will use java.exe from PATH, but set JAVA_HOME before building.
)

if not defined VULKAN_SDK (
    for /f "delims=" %%v in ('powershell -NoProfile -ExecutionPolicy Bypass -Command "$root='C:\VulkanSDK'; if(Test-Path $root){ Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName }"') do (
        set "VULKAN_SDK=%%v"
    )
)
if defined VULKAN_SDK (
    set "PATH=%VULKAN_SDK%\Bin;%PATH%"
)
where glslangValidator.exe >nul 2>nul || (
    echo [ERROR] glslangValidator.exe is required for Android offline shader compilation.
    echo [ERROR] Install the Vulkan SDK or rerun without --skip-winget to let winget install it.
    exit /b 1
)

set "GRADLEW=%ROOT%T850\android\gradlew.bat"
if not exist "%GRADLEW%" (
    echo [ERROR] Gradle wrapper was not found at "%GRADLEW%".
    echo [ERROR] Restore T850\android\gradlew.bat and gradle\wrapper before building Android.
    exit /b 1
)
if not exist "%ROOT%T850\android\gradle\wrapper\gradle-wrapper.jar" (
    echo [ERROR] Gradle wrapper jar is missing under T850\android\gradle\wrapper.
    exit /b 1
)
echo [T850] Gradle wrapper ready: %GRADLEW%

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

set "SDK_PACKAGES_READY=1"
if not exist "%ANDROID_SDK%\platform-tools\adb.exe" set "SDK_PACKAGES_READY=0"
if not exist "%ANDROID_SDK%\platforms\%ANDROID_PLATFORM%\android.jar" set "SDK_PACKAGES_READY=0"
if not exist "%ANDROID_SDK%\platforms\%ANDROID_MIN_PLATFORM%\android.jar" set "SDK_PACKAGES_READY=0"
if not exist "%ANDROID_SDK%\build-tools\%BUILD_TOOLS%\aapt2.exe" set "SDK_PACKAGES_READY=0"
if not exist "%ANDROID_SDK%\ndk\%NDK_VERSION%\source.properties" set "SDK_PACKAGES_READY=0"
if not exist "%ANDROID_SDK%\cmake\%CMAKE_VERSION%\bin\cmake.exe" set "SDK_PACKAGES_READY=0"

if "%SDK_PACKAGES_READY%"=="1" (
    echo [T850] Android SDK packages already installed; skipping sdkmanager package install.
) else (
    echo [T850] Accepting Android SDK licenses...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "1..20 | ForEach-Object { 'y' }" | call "%SDKMANAGER%" --sdk_root="%ANDROID_SDK%" --licenses
    if errorlevel 1 echo [WARN] License acceptance reported a non-zero exit code. Re-run this script if installs fail.

    echo [T850] Installing Android SDK packages...
    call "%SDKMANAGER%" --sdk_root="%ANDROID_SDK%" ^
        "platform-tools" ^
        "platforms;%ANDROID_PLATFORM%" ^
        "platforms;%ANDROID_MIN_PLATFORM%" ^
        "build-tools;%BUILD_TOOLS%" ^
        "ndk;%NDK_VERSION%" ^
        "cmake;%CMAKE_VERSION%"
    if errorlevel 1 (
        echo [ERROR] Android SDK package installation failed.
        exit /b 1
    )
)

if "%WITH_EMULATOR%"=="1" (
    echo [T850] Installing Android emulator packages...
    call "%SDKMANAGER%" --sdk_root="%ANDROID_SDK%" ^
        "emulator" ^
        "%EMULATOR_IMAGE%"
    if errorlevel 1 (
        echo [ERROR] Android emulator package installation failed.
        exit /b 1
    )
)

set "ANDROID_NDK_HOME=%ANDROID_SDK%\ndk\%NDK_VERSION%"
set "ANDROID_NDK_ROOT=%ANDROID_NDK_HOME%"

set "VMA_INCLUDE=%ROOT%T850\Librerias\VulkanMemoryAllocator\include\vma"
if not exist "%VMA_INCLUDE%\vk_mem_alloc.h" (
    echo [T850] Downloading Vulkan Memory Allocator header...
    mkdir "%VMA_INCLUDE%" 2>nul
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
      "$ErrorActionPreference='Stop'; Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.1.0/include/vk_mem_alloc.h' -OutFile '%VMA_INCLUDE%\vk_mem_alloc.h'"
    if errorlevel 1 (
        echo [ERROR] Failed to download Vulkan Memory Allocator.
        exit /b 1
    )
)

set "VCPKG_ROOT=%ROOT%T850\Librerias\vcpkg"
set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"
if not exist "%VCPKG_ROOT%\bootstrap-vcpkg.bat" (
    where git >nul 2>nul || (
        echo [ERROR] vcpkg was not found and Git is required to clone it.
        exit /b 1
    )
    echo [T850] Cloning vcpkg...
    git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone vcpkg.
        exit /b 1
    )
)
if not exist "%VCPKG_EXE%" (
    if exist "%VCPKG_ROOT%\bootstrap-vcpkg.bat" (
        echo [T850] Bootstrapping vcpkg...
        pushd "%VCPKG_ROOT%"
        call bootstrap-vcpkg.bat -disableMetrics
        popd
    ) else (
        echo [ERROR] vcpkg was not found at "%VCPKG_ROOT%".
        exit /b 1
    )
)
echo [T850] Installing Android vcpkg dependencies...
set "VCPKG_PACKAGES="
echo,%ANDROID_ABIS%| findstr /I /C:"arm64-v8a" >nul
if not errorlevel 1 (
    call :add_android_vcpkg_packages arm64-android
    if errorlevel 1 exit /b 1
)
echo,%ANDROID_ABIS%| findstr /I /C:"x86_64" >nul
if not errorlevel 1 (
    call :add_android_vcpkg_packages x64-android
    if errorlevel 1 exit /b 1
)
if not defined VCPKG_PACKAGES (
    echo [ERROR] Unsupported Android ABI list: %ANDROID_ABIS%
    echo [ERROR] Supported ABIs: arm64-v8a,x86_64
    exit /b 1
)
call "%VCPKG_EXE%" install !VCPKG_PACKAGES! --no-print-usage
if errorlevel 1 (
    echo [ERROR] Failed to install Android vcpkg dependencies.
    exit /b 1
)

echo.
echo [T850] Android setup complete.
echo.
echo Add these environment variables permanently if they are not already set:
echo   setx JAVA_HOME "%JAVA_HOME%"
echo   setx ANDROID_HOME "%ANDROID_HOME%"
echo   setx ANDROID_SDK_ROOT "%ANDROID_SDK_ROOT%"
echo   setx ANDROID_NDK_HOME "%ANDROID_NDK_HOME%"
echo   setx ANDROID_NDK_ROOT "%ANDROID_NDK_ROOT%"
if defined VULKAN_SDK echo   setx VULKAN_SDK "%VULKAN_SDK%"
echo.
echo Current session values:
echo   JAVA_HOME=%JAVA_HOME%
echo   ANDROID_HOME=%ANDROID_HOME%
echo   ANDROID_SDK_ROOT=%ANDROID_SDK_ROOT%
echo   ANDROID_NDK_HOME=%ANDROID_NDK_HOME%
echo   ANDROID_NDK_ROOT=%ANDROID_NDK_ROOT%
if defined VULKAN_SDK echo   VULKAN_SDK=%VULKAN_SDK%
echo.
echo Build Android package with:
echo   T850\scripts\android\BuildAndroid.bat Debug
echo   T850\scripts\android\BuildAndroid.bat Release
echo   T850\scripts\android\BuildAndroidDebug.bat
echo   T850\scripts\android\BuildAndroidRelease.bat
echo Add --clean for a full rebuild, or --install --launch for local device testing.
echo Add --emulator to T850\scripts\android\BuildAndroid.bat after running this setup with --with-emulator.
echo Add --android-abis arm64-v8a,x86_64 to install dependencies for multiple Android APK targets.
echo.
exit /b 0

:usage
echo Usage: SetupAndroidToolchain.bat [--sdk C:\Android\Sdk] [--skip-winget] [--with-emulator] [--android-abis arm64-v8a,x86_64]
exit /b 1

:add_android_vcpkg_packages
set "ANDROID_TRIPLET=%~1"
set "IMGUI_INCLUDE_DIR=%VCPKG_ROOT%\installed\%ANDROID_TRIPLET%\include"
set "IMGUI_NEEDS_REINSTALL=0"
if exist "%IMGUI_INCLUDE_DIR%\imgui.h" (
    if not exist "%IMGUI_INCLUDE_DIR%\imgui_impl_android.h" set "IMGUI_NEEDS_REINSTALL=1"
    if not exist "%IMGUI_INCLUDE_DIR%\imgui_impl_vulkan.h" set "IMGUI_NEEDS_REINSTALL=1"
    if "!IMGUI_NEEDS_REINSTALL!"=="1" (
        echo [T850] imgui:%ANDROID_TRIPLET% present but missing Android/Vulkan backend features -- removing for reinstall
        call "%VCPKG_EXE%" remove imgui:%ANDROID_TRIPLET% --recurse --no-print-usage
        if errorlevel 1 exit /b 1
    )
)
set "VCPKG_PACKAGES=!VCPKG_PACKAGES! draco:%ANDROID_TRIPLET% glslang:%ANDROID_TRIPLET% joltphysics:%ANDROID_TRIPLET% recastnavigation:%ANDROID_TRIPLET% imgui[android-binding,docking-experimental,vulkan-binding]:%ANDROID_TRIPLET%"
exit /b 0
