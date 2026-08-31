# Steam Deck Build and Deployment

Status: verified against SteamRT, package, launcher, cloud-asset, and Windows SSH orchestration scripts on 2026-08-19.

Steam Deck/Linux uses Vulkan and the CMake build. The official reproducible build runs inside Valve's Steam Runtime `sniper` SDK container through Podman with Clang 16 and libc++.

The Steam build consumes `Framework/CMakeLists.txt`, `FrameworkImGui/CMakeLists.txt`, `DayScene/CMakeLists.txt`, and, with `--with-editor`, `T8ditor/CMakeLists.txt`. From a Windows source checkout, run `scripts/ValidateBuildRegistration.ps1` before handing the tree to a SteamRT host.

## Official Local Build

Prerequisites on a Linux host or Steam Deck:

- Podman;
- Git checkout with vcpkg available/clonable;
- internet access for container packages and vcpkg on first build.

From the source root:

```bash
./steamdeck/BuildSteamRuntime.sh --configuration Release
```

Configure only:

```bash
./steamdeck/BuildSteamRuntime.sh --configuration Release --configure-only
```

Include T8ditor in CMake configuration:

```bash
./steamdeck/BuildSteamRuntime.sh --configuration Release --with-editor
```

Clean reproducible state first:

```bash
./steamdeck/BuildSteamRuntime.sh --configuration Release --clean
```

The script:

1. requires `podman`;
2. runs `registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest`;
3. installs Clang 16/libc++ and X11/Wayland/EGL development dependencies;
4. configures Ninja with `T850_PLATFORM_STEAM_DECK=ON`;
5. builds `DayScene` unless configure-only;
6. copies `libc++.so.1`, `libc++abi.so.1`, and `libunwind.so.1` beside the executable.

Output:

```text
T850/bin/SteamDeck/Release/DayScene
T850/bin/SteamDeck/Release/libc++.so.1
T850/bin/SteamDeck/Release/libc++abi.so.1
T850/bin/SteamDeck/Release/libunwind.so.1
```

A missing Podman message is an environment prerequisite failure before CMake, not a source failure.

## Run on Steam Deck/Linux

Use the runtime wrapper:

```bash
./steamdeck/T850.sh --game-mode
./steamdeck/T850.sh --desktop
```

Game mode defaults to fullscreen 1280x800. Desktop defaults to windowed 1280x800. Both force Vulkan and load `steamdeck/config_steamdeck.json`.

Options:

```text
--game-mode
--desktop
--scene N
--scene-file PATH | --t8scene PATH
--model PATH
--log-level error|info|debug|verbose|trace|0..4
--profile
--benchmark
-- <additional DayScene arguments>
```

Examples:

```bash
./steamdeck/T850.sh --game-mode --scene 1
./steamdeck/T850.sh --desktop --scene 4 --scene-file Scenes/DayScene.t8scene
./steamdeck/T850.sh --desktop -- --telemetry --telemetryOutput logs/deck.json
```

The wrapper:

- sets `LD_LIBRARY_PATH` to the runtime directory;
- downloads cloud assets unless `T850_SKIP_ASSET_DOWNLOAD=1`;
- creates runtime asset symlinks for Shaders, Models, Fonts, Textures, Scenes, and Layouts;
- sets SDL controller/minimize environment values;
- executes DayScene from the source/package root.

Asset download worker count uses `T850_CLOUD_DOWNLOAD_THREADS` (default 7).

## Windows-to-Deck Orchestrator

`LaunchSteamDeckSolution.bat` prepares a Windows host and a remote Deck over SSH.

Create ignored `deckConfig.json` beside the script:

```json
{
  "deckHost": "192.168.1.50",
  "deckUser": "deck",
  "deckRoot": "$HOME/Code/T850",
  "branch": "your-branch",
  "repoUrl": "https://github.com/0Camus0/T850.git"
}
```

Or pass host/user explicitly:

```powershell
.\LaunchSteamDeckSolution.bat --deck-host 192.168.1.50 --deck-user deck --setup-only
```

Common operations:

```powershell
# Prepare host and remote checkout/packages
.\LaunchSteamDeckSolution.bat --deck-host 192.168.1.50 --deck-user deck --setup-only

# Configure SteamRT CMake remotely
.\LaunchSteamDeckSolution.bat --deck-host 192.168.1.50 --deck-user deck --configure --setup-only

# Configure and build remotely
.\LaunchSteamDeckSolution.bat --deck-host 192.168.1.50 --deck-user deck --build --setup-only

# Copy local Assets/config.json
.\LaunchSteamDeckSolution.bat --deck-host 192.168.1.50 --deck-user deck --deploy-assets --skip-host --skip-deck --setup-only

# Install launcher and run
.\LaunchSteamDeckSolution.bat --deck-host 192.168.1.50 --deck-user deck --install-launcher --run-deck --skip-host --skip-deck --setup-only

# Desktop-mode run
.\LaunchSteamDeckSolution.bat --deck-host 192.168.1.50 --deck-user deck --run-desktop --skip-host --skip-deck --setup-only
```

### Update the Deck from a Windows working tree

`UpdateSteamDeck.ps1` is the direct development loop when the local changes are not committed or pushed yet. It packages `HEAD` plus local tracked and untracked overlays, excludes the vcpkg tree, updates a separate managed Deck directory, reuses dependencies from the base Deck checkout, runs the official SteamRT build, and refreshes desktop/Steam shortcuts.

Configure ignored `deckConfig.json` beside the script:

```json
{
  "deckHost": "192.168.1.50",
  "deckUser": "deck",
  "deckRoot": "/home/deck/Code/T850-minecraft-atlas-test",
  "baseDeckRoot": "/home/deck/Code/T850"
}
```

Then run from PowerShell:

```powershell
# Sync the current working tree, build SteamRT Release, and install shortcuts.
.\UpdateSteamDeck.ps1

# Also launch Minecraft as a supervised fullscreen Game Mode process.
.\UpdateSteamDeck.ps1 -Run

# Refresh source/assets and shortcuts without rebuilding.
.\UpdateSteamDeck.ps1 -SkipBuild
```

The managed `deckRoot` must differ from `baseDeckRoot`; the updater never resets or changes the branch of the base checkout. `-Desktop` changes the optional `-Run` launch to windowed 1280x800.

Remote setup requires Windows OpenSSH `ssh`/`scp` and key-based/no-password connectivity. When package setup is enabled, it may disable SteamOS read-only mode and run `sudo pacman`; review this before using it on a managed Deck.

Important switches:

```text
--deck-host, --deck-user, --deck-root
--branch, --repo, --configuration
--skip-winget, --skip-host, --skip-deck
--skip-deck-packages
--skip-deck-vcpkg (default), --install-deck-vcpkg
--skip-assets, --deploy-assets
--configure, --build, --with-editor
--install-launcher
--run-deck, --run-desktop
--no-pull
--setup-only
```

`--skip-deck` skips setup but explicit configure/build/deploy/run stages can still require Deck configuration.

## Package a Release

From the source root:

```bash
./steamdeck/PackageSteamDeckRelease.sh --configuration Release
```

Package an existing build:

```bash
./steamdeck/PackageSteamDeckRelease.sh --configuration Release --skip-build
```

Custom output:

```bash
./steamdeck/PackageSteamDeckRelease.sh --skip-build --output artifacts/T850-SteamDeck-Release.tar.gz
```

Default output:

```text
T850/steamdeck/package/T850-SteamDeck-Release.tar.gz
```

The tarball contains runtime binaries/libraries, launcher scripts, config, cloud downloader, available Assets subdirectories, and the model manifest. Runtime asset junctions/symlinks from the build output are removed before packaging so real package assets are used.

## Install Desktop/Steam Launcher

On the Deck checkout/package:

```bash
chmod +x T850/steamdeck/T850.sh T850/steamdeck/InstallSteamDeckLauncher.sh
T850/steamdeck/InstallSteamDeckLauncher.sh
```

The installer creates desktop/application launchers and can update Steam shortcuts using the supplied Python launcher tooling.

It installs three non-Steam library entries:

- **T850** for the default runtime;
- **T850 Minecraft** for `T850.sh --game-mode --scene 6`;
- **T850 Launcher** for the graphical scene/settings launcher.

The shortcut writer backs up `shortcuts.vdf` as `shortcuts.vdf.t850bak`. Restart Steam or switch back to Game Mode after installation so the Steam client reloads the shortcut database.

## CI Behavior

The Steam Deck GitHub Actions job:

1. installs Podman on Ubuntu;
2. validates shell/Python launcher syntax;
3. runs the SteamRT Release build;
4. verifies DayScene and the three bundled runtime libraries;
5. packages `T850-SteamDeck-Release.tar.gz`;
6. uploads the artifact and includes it in `v*` GitHub Releases.

## CMake Parity

Steam Deck does not consume Visual Studio project source lists. Every new Framework `.cpp` must be present in `Framework/CMakeLists.txt`. A Windows build passing does not prove Steam Deck linkage.

## Stop Conditions and Failures

| Failure | Classification/action |
|---|---|
| `podman is required` | environment prerequisite before configure |
| SteamRT image pull/apt failure | network/container environment |
| CMake cannot find a new game symbol | source omitted from CMake or dependency issue |
| missing libc++ beside DayScene | incomplete SteamRT build/package |
| SSH BatchMode fails | configure key-based SSH first |
| pacman/sudo fails | remote environment/SteamOS permissions |
| runtime misses assets | run wrapper, deploy/package assets, or inspect cloud download log |
| Vulkan startup fails | verify Deck drivers/Vulkan tools and inspect runtime log |

## Related Documents

- [Windows setup and build](../development/windows-build-and-run.md)
- [Cloud assets](../development/cloud-assets.md)
- [Verification gates](../testing/verification.md)
- [Runtime hosts](../runtime/runtime-hosts.md)
