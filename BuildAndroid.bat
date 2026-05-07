@echo off
setlocal enabledelayedexpansion

:: T850 Engine - Android APK build wrapper for local development.
:: Default build is incremental Debug. Use --clean to force a full rebuild.
:: Usage:
::   BuildAndroid.bat
::   BuildAndroid.bat Release
::   BuildAndroid.bat --configuration Release --clean
::   BuildAndroid.bat Debug --install --launch
::   BuildAndroid.bat --sdk C:\Android\Sdk
::   BuildAndroid.bat Debug --emulator
::   BuildAndroid.bat Debug --vulkan-validation

set "ROOT=%~dp0"
set "ANDROID_PROJECT=%ROOT%T850\android"
set "CONFIG=Debug"
set "CLEAN=0"
set "INSTALL=0"
set "LAUNCH=0"
set "ANDROID_SDK="
set "ABI_FILTERS=arm64-v8a"
set "GRADLE_VERSION=8.10.2"
set "NDK_VERSION=27.2.12479018"
set "VULKAN_VALIDATION=false"

:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="Debug" (
    set "CONFIG=Debug"
    shift
    goto parse_args
)
if /i "%~1"=="Release" (
    set "CONFIG=Release"
    shift
    goto parse_args
)
if /i "%~1"=="--configuration" goto arg_config
if /i "%~1"=="--config" goto arg_config
if /i "%~1"=="--sdk" goto arg_sdk
if /i "%~1"=="--abi" goto arg_abi
if /i "%~1"=="--abis" goto arg_abi
if /i "%~1"=="--emulator" (
    set "ABI_FILTERS=x86_64"
    shift
    goto parse_args
)
if /i "%~1"=="--vulkan-validation" (
    set "VULKAN_VALIDATION=true"
    shift
    goto parse_args
)
if /i "%~1"=="--clean" (
    set "CLEAN=1"
    shift
    goto parse_args
)
if /i "%~1"=="--install" (
    set "INSTALL=1"
    shift
    goto parse_args
)
if /i "%~1"=="--launch" (
    set "INSTALL=1"
    set "LAUNCH=1"
    shift
    goto parse_args
)
if /i "%~1"=="-h" goto usage
if /i "%~1"=="--help" goto usage
goto usage

:arg_config
shift
if "%~1"=="" goto usage
if /i "%~1"=="Debug" (
    set "CONFIG=Debug"
) else if /i "%~1"=="Release" (
    set "CONFIG=Release"
) else (
    goto usage
)
shift
goto parse_args

:arg_sdk
shift
if "%~1"=="" goto usage
set "ANDROID_SDK=%~1"
shift
goto parse_args

:arg_abi
shift
if "%~1"=="" goto usage
set "ABI_FILTERS=%~1"
shift
goto parse_args

:done_args

if not exist "%ANDROID_PROJECT%\build.gradle" (
    echo [ERROR] Android project was not found at "%ANDROID_PROJECT%".
    exit /b 1
)

if not defined ANDROID_SDK (
    if defined ANDROID_HOME set "ANDROID_SDK=%ANDROID_HOME%"
)
if not defined ANDROID_SDK (
    if defined ANDROID_SDK_ROOT set "ANDROID_SDK=%ANDROID_SDK_ROOT%"
)
if not defined ANDROID_SDK (
    set "ANDROID_SDK=%LOCALAPPDATA%\Android\Sdk"
)

if not defined JAVA_HOME (
    for /d %%j in ("%ProgramFiles%\Eclipse Adoptium\jdk-17*" "%ProgramFiles%\Java\jdk-17*") do (
        if exist "%%~fj\bin\java.exe" set "JAVA_HOME=%%~fj"
    )
)
if defined JAVA_HOME set "PATH=%JAVA_HOME%\bin;%PATH%"
where java >nul 2>nul || (
    echo [ERROR] JDK 17+ was not found. Run SetupAndroidToolchain.bat first.
    exit /b 1
)

set "ANDROID_HOME=%ANDROID_SDK%"
set "ANDROID_SDK_ROOT=%ANDROID_SDK%"
set "ANDROID_NDK_HOME=%ANDROID_SDK%\ndk\%NDK_VERSION%"
set "ANDROID_NDK_ROOT=%ANDROID_NDK_HOME%"
if not exist "%ANDROID_NDK_HOME%" (
    echo [ERROR] Android NDK %NDK_VERSION% was not found at "%ANDROID_NDK_HOME%".
    echo [ERROR] Run SetupAndroidToolchain.bat first, or pass --sdk to this script.
    exit /b 1
)

set "GRADLE_HOME=%ANDROID_SDK%\gradle\gradle-%GRADLE_VERSION%"
if exist "%GRADLE_HOME%\bin\gradle.bat" set "PATH=%GRADLE_HOME%\bin;%PATH%"
where gradle >nul 2>nul || (
    echo [ERROR] Gradle %GRADLE_VERSION% was not found. Run SetupAndroidToolchain.bat first.
    exit /b 1
)

if not defined VULKAN_SDK (
    for /f "delims=" %%v in ('powershell -NoProfile -ExecutionPolicy Bypass -Command "$root='C:\VulkanSDK'; if(Test-Path $root){ Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName }"') do (
        set "VULKAN_SDK=%%v"
    )
)
if defined VULKAN_SDK set "PATH=%VULKAN_SDK%\Bin;%PATH%"
where glslangValidator.exe >nul 2>nul || (
    echo [ERROR] glslangValidator.exe was not found. Install the Vulkan SDK or set VULKAN_SDK.
    exit /b 1
)

set "BUILD_TASK=assemble%CONFIG%"
set "INSTALL_TASK=install%CONFIG%"
if /i "%CONFIG%"=="Debug" (
    set "OUTPUT_VARIANT=debug"
) else (
    set "OUTPUT_VARIANT=release"
)

echo(
echo ========================================
echo  T850 - Android %CONFIG% Build
echo ========================================
echo SDK root: %ANDROID_SDK%
echo Project : %ANDROID_PROJECT%
echo ABIs    : %ABI_FILTERS%
echo Vulkan validation: %VULKAN_VALIDATION%
if "%CLEAN%"=="1" echo Mode    : clean rebuild
if not "%CLEAN%"=="1" echo Mode    : incremental
echo(

pushd "%ANDROID_PROJECT%" || exit /b 1

if "%CLEAN%"=="1" (
    call gradle --no-daemon --console=plain clean
    if errorlevel 1 (
        popd
        exit /b 1
    )
)

call gradle --no-daemon --console=plain "-Pt850AndroidAbis=%ABI_FILTERS%" "-Pt850VulkanValidation=%VULKAN_VALIDATION%" "%BUILD_TASK%"
if errorlevel 1 (
    popd
    exit /b 1
)

if "%INSTALL%"=="1" (
    call gradle --no-daemon --console=plain "-Pt850AndroidAbis=%ABI_FILTERS%" "-Pt850VulkanValidation=%VULKAN_VALIDATION%" "%INSTALL_TASK%"
    if errorlevel 1 (
        popd
        exit /b 1
    )
)

popd

if "%LAUNCH%"=="1" (
    set "ADB=%ANDROID_SDK%\platform-tools\adb.exe"
    if not exist "!ADB!" (
        echo [ERROR] adb.exe was not found at "!ADB!".
        exit /b 1
    )
    "!ADB!" shell am start -n com.t850.engine/.LauncherActivity
    if errorlevel 1 exit /b 1
)

set "APK_PATH=%ANDROID_PROJECT%\app\build\outputs\apk\%OUTPUT_VARIANT%\app-%OUTPUT_VARIANT%.apk"
if not exist "%APK_PATH%" if /i "%OUTPUT_VARIANT%"=="release" (
    set "APK_PATH=%ANDROID_PROJECT%\app\build\outputs\apk\release\app-release-unsigned.apk"
)

echo(
echo [T850] Android %CONFIG% build complete.
echo(
echo APK: %APK_PATH%
exit /b 0

:usage
echo Usage: BuildAndroid.bat [Debug^|Release] [--configuration Debug^|Release] [--sdk C:\Android\Sdk] [--abi ABI[,ABI...]] [--emulator] [--vulkan-validation] [--clean] [--install] [--launch]
exit /b 1
