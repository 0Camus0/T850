@echo off
setlocal enabledelayedexpansion

:: T850 Engine - Steam Deck development setup and launch helper.
:: Installs Windows host tools, downloads local runtime assets, prepares the
:: Steam Deck over SSH, and opens the Visual Studio solution for development.
:: The Linux/Steam Deck CMake target is intentionally opt-in until the port is
:: wired into T850\CMakeLists.txt.

set "ROOT=%~dp0"
set "SOLUTION=%ROOT%T850\T850.sln"
set "HOST_LAUNCHER=%ROOT%LaunchSolution.bat"
set "DECK_CONFIG=%ROOT%deckConfig.json"
set "DECK_HOST="
set "DECK_USER="
set "DECK_ROOT=$HOME/Code/T850"
set "BRANCH=editor_spline_deck_enablement"
set "REPO_URL=https://github.com/0Camus0/T850.git"
set "CONFIG=Release"

set "SKIP_WINGET=0"
set "SKIP_HOST=0"
set "SKIP_DECK=0"
set "SKIP_DECK_PACKAGES=0"
set "SKIP_DECK_VCPKG=1"
set "SKIP_ASSETS=0"
set "SKIP_LAUNCH=0"
set "CONFIGURE_DECK=0"
set "BUILD_DECK=0"
set "DEPLOY_ASSETS=0"
set "PULL_DECK=1"
set "USAGE_EXIT=1"
set "BUILD_EDITOR=OFF"
set "INSTALL_DECK_LAUNCHER=0"
set "RUN_DECK=0"
set "RUN_MODE=game"

call :load_deck_config
if errorlevel 1 exit /b 1

:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="--deck-host" goto arg_deck_host
if /i "%~1"=="--host" goto arg_deck_host
if /i "%~1"=="--deck-user" goto arg_deck_user
if /i "%~1"=="--user" goto arg_deck_user
if /i "%~1"=="--deck-root" goto arg_deck_root
if /i "%~1"=="--branch" goto arg_branch
if /i "%~1"=="--repo" goto arg_repo
if /i "%~1"=="--configuration" goto arg_config
if /i "%~1"=="--config" goto arg_config
if /i "%~1"=="--skip-winget" set "SKIP_WINGET=1"& shift& goto parse_args
if /i "%~1"=="--skip-host" set "SKIP_HOST=1"& shift& goto parse_args
if /i "%~1"=="--skip-deck" set "SKIP_DECK=1"& shift& goto parse_args
if /i "%~1"=="--skip-deck-packages" set "SKIP_DECK_PACKAGES=1"& shift& goto parse_args
if /i "%~1"=="--skip-deck-vcpkg" set "SKIP_DECK_VCPKG=1"& shift& goto parse_args
if /i "%~1"=="--install-deck-vcpkg" set "SKIP_DECK_VCPKG=0"& shift& goto parse_args
if /i "%~1"=="--skip-assets" set "SKIP_ASSETS=1"& shift& goto parse_args
if /i "%~1"=="--setup-only" set "SKIP_LAUNCH=1"& shift& goto parse_args
if /i "%~1"=="--configure" set "CONFIGURE_DECK=1"& shift& goto parse_args
if /i "%~1"=="--build" set "CONFIGURE_DECK=1"& set "BUILD_DECK=1"& shift& goto parse_args
if /i "%~1"=="--with-editor" set "BUILD_EDITOR=ON"& shift& goto parse_args
if /i "%~1"=="--deploy-assets" set "DEPLOY_ASSETS=1"& shift& goto parse_args
if /i "%~1"=="--install-launcher" set "INSTALL_DECK_LAUNCHER=1"& shift& goto parse_args
if /i "%~1"=="--run-deck" set "RUN_DECK=1"& shift& goto parse_args
if /i "%~1"=="--run-desktop" set "RUN_DECK=1"& set "RUN_MODE=desktop"& shift& goto parse_args
if /i "%~1"=="--no-pull" set "PULL_DECK=0"& shift& goto parse_args
if /i "%~1"=="--help" set "USAGE_EXIT=0"& goto usage
if /i "%~1"=="-h" set "USAGE_EXIT=0"& goto usage
goto usage

:arg_deck_host
shift
if "%~1"=="" goto usage
set "DECK_HOST=%~1"
shift
goto parse_args

:arg_deck_user
shift
if "%~1"=="" goto usage
set "DECK_USER=%~1"
shift
goto parse_args

:arg_deck_root
shift
if "%~1"=="" goto usage
set "DECK_ROOT=%~1"
shift
goto parse_args

:arg_branch
shift
if "%~1"=="" goto usage
set "BRANCH=%~1"
shift
goto parse_args

:arg_repo
shift
if "%~1"=="" goto usage
set "REPO_URL=%~1"
shift
goto parse_args

:arg_config
shift
if "%~1"=="" goto usage
set "CONFIG=%~1"
shift
goto parse_args

:done_args

set "NEEDS_DECK=0"
if "%SKIP_DECK%"=="0" set "NEEDS_DECK=1"
if "%DEPLOY_ASSETS%"=="1" set "NEEDS_DECK=1"
if "%CONFIGURE_DECK%"=="1" set "NEEDS_DECK=1"
if "%BUILD_DECK%"=="1" set "NEEDS_DECK=1"
if "%INSTALL_DECK_LAUNCHER%"=="1" set "NEEDS_DECK=1"
if "%RUN_DECK%"=="1" set "NEEDS_DECK=1"
if "%NEEDS_DECK%"=="1" (
    call :require_deck_config
    if errorlevel 1 exit /b 1
)

echo.
echo ========================================
echo  T850 - Steam Deck Development Setup
echo ========================================
echo Host root : %ROOT%
echo Deck     : %DECK_USER%@%DECK_HOST%
echo Deck root: %DECK_ROOT%
echo Branch   : %BRANCH%
echo Config   : %CONFIG%
echo Editor   : %BUILD_EDITOR%
echo Run mode : %RUN_MODE%
echo.

if "%SKIP_HOST%"=="0" (
    call :setup_host
    if errorlevel 1 exit /b 1
) else (
    echo [T850] Skipping Windows host setup.
)

if "%SKIP_DECK%"=="0" (
    call :setup_deck
    if errorlevel 1 exit /b 1
) else (
    echo [T850] Skipping Steam Deck setup.
)

if "%DEPLOY_ASSETS%"=="1" (
    call :deploy_assets
    if errorlevel 1 exit /b 1
)

if "%CONFIGURE_DECK%"=="1" (
    call :configure_deck
    if errorlevel 1 exit /b 1
)

if "%BUILD_DECK%"=="1" (
    call :build_deck
    if errorlevel 1 exit /b 1
)

if "%INSTALL_DECK_LAUNCHER%"=="1" (
    call :install_deck_launcher
    if errorlevel 1 exit /b 1
)

if "%RUN_DECK%"=="1" (
    call :run_deck
    if errorlevel 1 exit /b 1
)

if "%SKIP_LAUNCH%"=="1" (
    echo.
    echo [T850] Steam Deck setup complete. Solution launch skipped.
    exit /b 0
)

echo.
echo [T850] Opening solution: %SOLUTION%
start "" "%SOLUTION%"
exit /b 0

:load_deck_config
if not exist "%DECK_CONFIG%" exit /b 0

for /f "usebackq tokens=1,* delims==" %%A in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $cfg=Get-Content -Raw -LiteralPath $env:DECK_CONFIG | ConvertFrom-Json; foreach($name in 'deckHost','deckUser','deckRoot','branch','repoUrl'){ $prop=$cfg.PSObject.Properties[$name]; if($null -ne $prop -and $null -ne $prop.Value -and [string]$prop.Value -ne ''){ '{0}={1}' -f $name, [string]$prop.Value } }"`) do (
    if /i "%%A"=="deckHost" set "DECK_HOST=%%B"
    if /i "%%A"=="deckUser" set "DECK_USER=%%B"
    if /i "%%A"=="deckRoot" set "DECK_ROOT=%%B"
    if /i "%%A"=="branch" set "BRANCH=%%B"
    if /i "%%A"=="repoUrl" set "REPO_URL=%%B"
)
if errorlevel 1 (
    echo [ERROR] Failed to read %DECK_CONFIG%.
    exit /b 1
)
exit /b 0

:require_deck_config
if "%DECK_HOST%"=="" (
    echo [ERROR] Steam Deck host is not configured.
    echo [ERROR] Create local deckConfig.json beside this script with deckHost/deckUser, or pass --deck-host and --deck-user.
    exit /b 1
)
if "%DECK_USER%"=="" (
    echo [ERROR] Steam Deck user is not configured.
    echo [ERROR] Create local deckConfig.json beside this script with deckHost/deckUser, or pass --deck-host and --deck-user.
    exit /b 1
)
exit /b 0

:setup_host
echo.
echo ========================================
echo  T850 - Windows host prerequisites
echo ========================================

where powershell >nul 2>nul || (
    echo [ERROR] PowerShell is required.
    exit /b 1
)

if "%SKIP_WINGET%"=="0" (
    where winget >nul 2>nul
    if not errorlevel 1 (
        echo [T850] Installing host tools with winget when missing...
        winget install --id Git.Git -e --accept-package-agreements --accept-source-agreements
        winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
        winget install --id Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements
        winget install --id KhronosGroup.VulkanSDK -e --accept-package-agreements --accept-source-agreements
    ) else (
        echo [WARN] winget not found. Install Git, CMake, Ninja, and the Vulkan SDK manually.
    )
) else (
    echo [T850] Skipping winget host package installation.
)

where git >nul 2>nul || (
    echo [ERROR] Git is required.
    exit /b 1
)

if not "%SKIP_ASSETS%"=="1" (
    if not exist "%HOST_LAUNCHER%" (
        echo [ERROR] Host launcher not found: %HOST_LAUNCHER%
        exit /b 1
    )
    echo [T850] Installing Windows vcpkg packages and downloading runtime assets...
    call "%HOST_LAUNCHER%" --setup-only
    if errorlevel 1 exit /b 1
) else (
    if not exist "%HOST_LAUNCHER%" (
        echo [ERROR] Host launcher not found: %HOST_LAUNCHER%
        exit /b 1
    )
    echo [T850] Installing Windows vcpkg packages; cloud assets skipped...
    call "%HOST_LAUNCHER%" --setup-only --skip-assets
    if errorlevel 1 exit /b 1
)

exit /b 0

:setup_deck
echo.
echo ========================================
echo  T850 - Steam Deck prerequisites
echo ========================================

where ssh >nul 2>nul || (
    echo [ERROR] ssh.exe was not found. Install Windows OpenSSH Client.
    exit /b 1
)

echo [T850] Checking SSH connection to %DECK_USER%@%DECK_HOST%...
ssh -o BatchMode=yes -o ConnectTimeout=10 "%DECK_USER%@%DECK_HOST%" "uname -a"
if errorlevel 1 (
    echo [ERROR] Could not connect to %DECK_USER%@%DECK_HOST% with key-based/no-password SSH.
    exit /b 1
)

if "%SKIP_DECK_PACKAGES%"=="0" (
    echo [T850] Installing SteamOS build and Vulkan packages...
    call :deck_ssh "if command -v pacman >/dev/null 2>&1; then if command -v steamos-readonly >/dev/null 2>&1; then sudo -n steamos-readonly disable || sudo steamos-readonly disable; fi; sudo -n pacman -S --needed --noconfirm base-devel autoconf-archive cmake ninja git pkgconf vulkan-headers vulkan-tools vulkan-validation-layers mesa glslang shaderc sdl3 wayland wayland-protocols libxkbcommon libxft ibus libibus libglvnd || sudo pacman -S --needed --noconfirm base-devel autoconf-archive cmake ninja git pkgconf vulkan-headers vulkan-tools vulkan-validation-layers mesa glslang shaderc sdl3 wayland wayland-protocols libxkbcommon libxft ibus libibus libglvnd; else echo '[WARN] pacman not found; install build-essential, cmake, ninja, git, Vulkan SDK/tools, glslang, shaderc, SDL3, and Wayland development packages manually.'; fi"
    if errorlevel 1 exit /b 1
) else (
    echo [T850] Skipping Steam Deck system package installation.
)

if "%PULL_DECK%"=="1" (
    echo [T850] Syncing branch %BRANCH% on Steam Deck...
    call :deck_ssh "mkdir -p $(dirname %DECK_ROOT%) && if [ -d %DECK_ROOT%/.git ]; then :; else git clone %REPO_URL% %DECK_ROOT%; fi && cd %DECK_ROOT% && git fetch origin %BRANCH% && git checkout %BRANCH% && git pull --ff-only"
    if errorlevel 1 exit /b 1
) else (
    echo [T850] Skipping Steam Deck git sync.
)

if "%SKIP_DECK_VCPKG%"=="0" (
    echo [T850] Installing Steam Deck vcpkg dependencies...
    call :deck_ssh "cd %DECK_ROOT% && if [ -f T850/Librerias/vcpkg/bootstrap-vcpkg.sh ]; then :; else rm -rf T850/Librerias/vcpkg && git clone https://github.com/microsoft/vcpkg.git T850/Librerias/vcpkg; fi && cd T850/Librerias/vcpkg && ./bootstrap-vcpkg.sh -disableMetrics && ./vcpkg install --recurse --no-print-usage sdl3[vulkan,wayland,x11]:x64-linux vulkan-headers:x64-linux vulkan-loader:x64-linux vulkan-memory-allocator:x64-linux glslang:x64-linux draco:x64-linux joltphysics:x64-linux recastnavigation:x64-linux imgui[docking-experimental,vulkan-binding,opengl3-binding,sdl3-binding]:x64-linux imguizmo:x64-linux"
    if errorlevel 1 exit /b 1
) else (
    echo [T850] Skipping Steam Deck vcpkg dependency preinstall. SteamRT CMake installs vcpkg dependencies from its manifest.
)

exit /b 0

:deploy_assets
echo.
echo ========================================
echo  T850 - Deploying assets to Steam Deck
echo ========================================

where scp >nul 2>nul || (
    echo [ERROR] scp.exe was not found. Install Windows OpenSSH Client.
    exit /b 1
)

if not exist "%ROOT%T850\Assets" (
    echo [ERROR] Local assets folder not found: %ROOT%T850\Assets
    exit /b 1
)

call :deck_ssh "mkdir -p %DECK_ROOT%/T850"
if errorlevel 1 exit /b 1

echo [T850] Copying T850\Assets to Steam Deck...
scp -r "%ROOT%T850\Assets" "%DECK_USER%@%DECK_HOST%:%DECK_ROOT%/T850/"
if errorlevel 1 exit /b 1

if exist "%ROOT%T850\config.json" (
    scp "%ROOT%T850\config.json" "%DECK_USER%@%DECK_HOST%:%DECK_ROOT%/T850/config.json"
    if errorlevel 1 exit /b 1
)

exit /b 0

:configure_deck
echo.
echo ========================================
echo  T850 - Configuring Steam Deck CMake
echo ========================================

if /i "%BUILD_EDITOR%"=="ON" (
    call :deck_ssh "cd %DECK_ROOT% && chmod +x T850/steamdeck/BuildSteamRuntime.sh && T850/steamdeck/BuildSteamRuntime.sh --configuration %CONFIG% --with-editor --configure-only"
) else (
    call :deck_ssh "cd %DECK_ROOT% && chmod +x T850/steamdeck/BuildSteamRuntime.sh && T850/steamdeck/BuildSteamRuntime.sh --configuration %CONFIG% --configure-only"
)
exit /b %ERRORLEVEL%

:build_deck
echo.
echo ========================================
echo  T850 - Building on Steam Deck
echo ========================================

if /i "%BUILD_EDITOR%"=="ON" (
    call :deck_ssh "cd %DECK_ROOT% && chmod +x T850/steamdeck/BuildSteamRuntime.sh && T850/steamdeck/BuildSteamRuntime.sh --configuration %CONFIG% --with-editor"
) else (
    call :deck_ssh "cd %DECK_ROOT% && chmod +x T850/steamdeck/BuildSteamRuntime.sh && T850/steamdeck/BuildSteamRuntime.sh --configuration %CONFIG%"
)
exit /b %ERRORLEVEL%

:install_deck_launcher
echo.
echo ========================================
echo  T850 - Installing Steam Deck launcher
echo ========================================

call :deck_ssh "cd %DECK_ROOT% && chmod +x T850/steamdeck/T850.sh T850/steamdeck/InstallSteamDeckLauncher.sh && T850/steamdeck/InstallSteamDeckLauncher.sh"
exit /b %ERRORLEVEL%

:run_deck
echo.
echo ========================================
echo  T850 - Running on Steam Deck
echo ========================================

if /i "%RUN_MODE%"=="desktop" (
    call :deck_ssh "cd %DECK_ROOT% && chmod +x T850/steamdeck/T850.sh && T850/steamdeck/T850.sh --desktop"
) else (
    call :deck_ssh "cd %DECK_ROOT% && chmod +x T850/steamdeck/T850.sh && T850/steamdeck/T850.sh --game-mode"
)
exit /b %ERRORLEVEL%

:deck_ssh
ssh -o BatchMode=yes "%DECK_USER%@%DECK_HOST%" "%~1"
exit /b %ERRORLEVEL%

:usage
echo Usage:
echo   LaunchSteamDeckSolution.bat [options]
echo.
echo Options:
echo   --deck-host IP              Steam Deck IP address. Overrides local deckConfig.json.
echo   --deck-user USER            Steam Deck SSH user. Overrides local deckConfig.json.
echo   --deck-root PATH            Remote checkout path. Default: $HOME/Code/T850
echo   --branch NAME               Remote branch to sync. Default: editor_spline_deck_enablement
echo   --repo URL                  Repository URL to clone on the Deck.
echo   --configuration NAME        CMake config/build type. Default: Release
echo   --skip-winget               Do not install Windows host tools with winget.
echo   --skip-host                 Skip Windows host dependency and asset setup.
echo   --skip-deck                 Skip all Steam Deck SSH setup.
echo   --skip-deck-packages        Skip SteamOS pacman package installation.
echo   --skip-deck-vcpkg           Skip Steam Deck vcpkg dependency preinstall. This is the default.
echo   --install-deck-vcpkg        Preinstall legacy Deck vcpkg dependencies before CMake.
echo   --skip-assets               Skip local cloud asset download.
echo   --deploy-assets             Copy local T850\Assets and config.json to the Deck.
echo   --configure                 Run the Steam Deck CMake configure step.
echo   --build                     Configure and build on the Steam Deck.
echo   --with-editor               Include T8ditor in the Steam Deck CMake build.
echo   --install-launcher          Install the Deck desktop launcher entry.
echo   --run-deck                  Run DayScene on the Deck in Game Mode defaults.
echo   --run-desktop               Run DayScene on the Deck in Desktop Mode defaults.
echo   --no-pull                   Do not clone/fetch/pull the remote Deck checkout.
echo   --setup-only                Do not open the Visual Studio solution.
echo   -h, --help                  Show this help.
echo.
echo Local Deck connection settings are read from ignored deckConfig.json when present.
echo Supported fields: deckHost, deckUser, deckRoot, branch, repoUrl.
exit /b %USAGE_EXIT%
