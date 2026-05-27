@echo off
setlocal enabledelayedexpansion

:: T850 Engine - Android APK build wrapper for local development.
:: Default build is incremental Debug. Use --clean to force a full rebuild.
:: Usage:
::   Scripts\Bat\BuildAndroid.bat
::   Scripts\Bat\BuildAndroid.bat Release
::   Scripts\Bat\BuildAndroid.bat --configuration Release --clean
::   Scripts\Bat\BuildAndroid.bat Debug --install --launch
::   Scripts\Bat\BuildAndroid.bat --sdk C:\Android\Sdk
::   Scripts\Bat\BuildAndroid.bat Debug --emulator
::   Scripts\Bat\BuildAndroid.bat Debug --vulkan-validation
::   Scripts\Bat\BuildAndroid.bat Release --allow-unsigned-release

set "ROOT=%~dp0..\..\"
set "ANDROID_PROJECT=%ROOT%T850\android"
set "CONFIG=Debug"
set "CLEAN=0"
set "INSTALL=0"
set "LAUNCH=0"
set "ANDROID_SDK="
set "ABI_FILTERS=arm64-v8a"
set "NDK_VERSION=27.2.12479018"
set "VULKAN_VALIDATION=false"
set "ASSET_PROFILE=physics-demo"
set "ALLOW_UNSIGNED_RELEASE=0"

set "HOST_CORES=%NUMBER_OF_PROCESSORS%"
if not defined HOST_CORES set "HOST_CORES=1"
set /a DEFAULT_BUILD_WORKERS=HOST_CORES-1
if %DEFAULT_BUILD_WORKERS% LSS 1 set "DEFAULT_BUILD_WORKERS=1"
if not defined T850_BUILD_WORKERS set "T850_BUILD_WORKERS=%DEFAULT_BUILD_WORKERS%"
set "CMAKE_BUILD_PARALLEL_LEVEL=%T850_BUILD_WORKERS%"

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
if /i "%~1"=="--asset-profile" goto arg_asset_profile
if /i "%~1"=="--allow-unsigned-release" (
    set "ALLOW_UNSIGNED_RELEASE=1"
    shift
    goto parse_args
)
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
:arg_abi_more
if "%~1"=="" goto parse_args
set "NEXT_ARG=%~1"
if "!NEXT_ARG:~0,1!"=="-" goto parse_args
if /i "%~1"=="Debug" goto parse_args
if /i "%~1"=="Release" goto parse_args
set "ABI_FILTERS=!ABI_FILTERS!,%~1"
shift
goto arg_abi_more

:arg_asset_profile
shift
if "%~1"=="" goto usage
set "ASSET_PROFILE=%~1"
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
if defined JAVA_HOME set "PATH=%JAVA_HOME%\bin;%PATH%"
where java >nul 2>nul || (
    echo [ERROR] JDK 17+ was not found. Run ..\..\SetupAndroidToolchain.bat first.
    exit /b 1
)

set "ANDROID_HOME=%ANDROID_SDK%"
set "ANDROID_SDK_ROOT=%ANDROID_SDK%"
set "ANDROID_NDK_HOME=%ANDROID_SDK%\ndk\%NDK_VERSION%"
set "ANDROID_NDK_ROOT=%ANDROID_NDK_HOME%"
if not exist "%ANDROID_NDK_HOME%" (
    echo [ERROR] Android NDK %NDK_VERSION% was not found at "%ANDROID_NDK_HOME%".
    echo [ERROR] Run ..\..\SetupAndroidToolchain.bat first, or pass --sdk to this script.
    exit /b 1
)

set "GRADLEW=%ANDROID_PROJECT%\gradlew.bat"
if not exist "%GRADLEW%" (
    echo [ERROR] Gradle wrapper was not found at "%GRADLEW%".
    echo [ERROR] Restore T850\android\gradlew.bat and gradle\wrapper before building.
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
    if not "%ALLOW_UNSIGNED_RELEASE%"=="1" (
        call :check_release_signing
        if errorlevel 1 exit /b 1
    )
)

echo(
echo ========================================
echo  T850 - Android %CONFIG% Build
echo ========================================
echo SDK root: %ANDROID_SDK%
echo Project : %ANDROID_PROJECT%
echo ABIs    : %ABI_FILTERS%
echo Assets  : %ASSET_PROFILE%
echo Vulkan validation: %VULKAN_VALIDATION%
echo Workers : %T850_BUILD_WORKERS% ^(cores - 1^)
if "%ALLOW_UNSIGNED_RELEASE%"=="1" echo Release signing: unsigned allowed
if "%CLEAN%"=="1" echo Mode    : clean rebuild
if not "%CLEAN%"=="1" echo Mode    : incremental
echo(

pushd "%ANDROID_PROJECT%" || exit /b 1

if "%CLEAN%"=="1" (
    call "%GRADLEW%" --no-daemon --console=plain --parallel --max-workers=%T850_BUILD_WORKERS% clean
    if errorlevel 1 (
        popd
        exit /b 1
    )
)

call "%GRADLEW%" --no-daemon --console=plain --parallel --max-workers=%T850_BUILD_WORKERS% "-Pt850AndroidAbis=%ABI_FILTERS%" "-Pt850VulkanValidation=%VULKAN_VALIDATION%" "-Pt850AndroidAssetProfile=%ASSET_PROFILE%" "-Pt850AllowUnsignedRelease=%ALLOW_UNSIGNED_RELEASE%" "%BUILD_TASK%"
if errorlevel 1 (
    popd
    exit /b 1
)

if "%INSTALL%"=="1" (
    call "%GRADLEW%" --no-daemon --console=plain --parallel --max-workers=%T850_BUILD_WORKERS% "-Pt850AndroidAbis=%ABI_FILTERS%" "-Pt850VulkanValidation=%VULKAN_VALIDATION%" "-Pt850AndroidAssetProfile=%ASSET_PROFILE%" "-Pt850AllowUnsignedRelease=%ALLOW_UNSIGNED_RELEASE%" "%INSTALL_TASK%"
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
    "!ADB!" shell am force-stop com.t850.engine
    if errorlevel 1 exit /b 1
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

:check_release_signing
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $project='%ANDROID_PROJECT%'; $app=Join-Path $project 'app'; $props=@{}; $files=@((Join-Path $project 'signing.properties'),(Join-Path $app 'signing.properties'),(Join-Path $env:USERPROFILE '.android\t850-release-signing.properties')); foreach($file in $files){ if(Test-Path $file){ Get-Content $file | ForEach-Object { $line=$_.Trim(); if($line -and (-not $line.StartsWith('#')) -and $line.Contains('=')){ $idx=$line.IndexOf('='); $props[$line.Substring(0,$idx).Trim()]=$line.Substring($idx+1).Trim() } } } }; function Get-SigningValue([string[]]$names){ foreach($name in $names){ $value=[Environment]::GetEnvironmentVariable($name); if($value){ return $value }; if($props.ContainsKey($name) -and $props[$name]){ return $props[$name] } }; return $null }; $store=Get-SigningValue @('T850_RELEASE_STORE_FILE','ANDROID_KEYSTORE_PATH'); $storePass=Get-SigningValue @('T850_RELEASE_STORE_PASSWORD','ANDROID_KEYSTORE_PASSWORD'); $alias=Get-SigningValue @('T850_RELEASE_KEY_ALIAS','ANDROID_KEY_ALIAS'); $keyPass=Get-SigningValue @('T850_RELEASE_KEY_PASSWORD','ANDROID_KEY_PASSWORD'); $missing=@(); if(-not $store){$missing+='ANDROID_KEYSTORE_PATH or T850_RELEASE_STORE_FILE'}; if(-not $storePass){$missing+='ANDROID_KEYSTORE_PASSWORD or T850_RELEASE_STORE_PASSWORD'}; if(-not $alias){$missing+='ANDROID_KEY_ALIAS or T850_RELEASE_KEY_ALIAS'}; if(-not $keyPass){$missing+='ANDROID_KEY_PASSWORD or T850_RELEASE_KEY_PASSWORD'}; if($missing.Count){ Write-Host ('[ERROR] Release signing is not configured. Missing: ' + ($missing -join ', ')); Write-Host '[ERROR] Set environment variables or create android\signing.properties, android\app\signing.properties, or ~/.android/t850-release-signing.properties.'; exit 1 }; $storePath=$store; if(-not ([IO.Path]::IsPathRooted($storePath))){ $candidate=Join-Path $app $storePath; if(Test-Path $candidate){ $storePath=$candidate } else { $candidate=Join-Path $project $storePath; if(Test-Path $candidate){ $storePath=$candidate } } }; if(-not (Test-Path $storePath)){ Write-Host ('[ERROR] Release keystore was not found: ' + $store); exit 1 }"
if errorlevel 1 (
    echo [ERROR] Android Release builds must be signed. Configure signing and retry.
    exit /b 1
)
exit /b 0

:usage
echo Usage: Scripts\Bat\BuildAndroid.bat [Debug^|Release] [--configuration Debug^|Release] [--sdk C:\Android\Sdk] [--abi ABI[,ABI...]] [--asset-profile physics-demo^|doom-porsche^|q3-sandbox^|models-full^|full] [--emulator] [--vulkan-validation] [--allow-unsigned-release] [--clean] [--install] [--launch]
exit /b 1
