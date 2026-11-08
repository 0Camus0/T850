# T850 on Ubuntu — DayScene build/run notes

## Host

- Ubuntu 26.04 x86_64, gcc 15.2, cmake, ninja
- GPU: NVIDIA RTX PRO 6000 Blackwell, driver 615.71.09

## Install

```bash
sudo apt-get install -y libxft-dev libfreetype-dev   # pulls libfontconfig-dev/libfontconfig1-dev
```

## Configure

```bash
cd T850
cmake -S . -B build/linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DT850_PLATFORM_STEAM_DECK=ON -DT850_BUILD_EDITOR=OFF
```

Auto-detects Linux x86_64 → Steam Deck platform. Bootstraps vcpkg in
`Librerias/vcpkg` and installs the x64-linux manifest: draco, glslang,
joltphysics, recastnavigation, vulkan-headers/loader/memory-allocator,
sdl3[vulkan,wayland,x11], imgui.

## Build

```bash
cmake --build build/linux --target DayScene --parallel 24
```

→ 171 tasks, 0 errors. Output: `bin/SteamDeck/Release/DayScene`.

## Assets

```bash
python3 steamdeck/DownloadCloudAssets.py --root . --jobs 7
```

167 files, sha256-verified. Creates untracked `Assets/.t850-cloud-assets.json`.

## Run

```bash
cd bin/SteamDeck/Release
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./DayScene --unattended --api vulkan \
  --scene 7 --width 1280 --height 720 --dump-frame 60 --logLevel info
```

`LD_LIBRARY_PATH` is required so SDL3 uses the **system** Vulkan loader
(`libvulkan.so.1.4.341`); the vcpkg-linked loader omits
`VK_KHR_wayland_surface`/xcb/xlib and SDL window creation fails at
`LinuxFramework.cpp:275`.

## Launcher (GUI)

The WPF `scripts/Launcher.ps1` / `T850Launcher.exe` are Windows-only. On Linux the launcher
is the GTK one:

```bash
cd /home/camus000/testBitso/T850
./steamdeck/T850DeckLauncher.sh
```

Scene list: Sandbox, Day Scene, Quake3 Mock, Ragdoll Editor, Scene Template, Minecraft,
Backyard (7, VOF), **Backyard 8 (PBF)**. The preselected scene and window size come from
`steamdeck/config_steamdeck.json`. To run it detached from a terminal that you then close:

```bash
cd /home/camus000/testBitso/T850
setsid ./steamdeck/T850DeckLauncher.sh > /tmp/opencode/t850-launcher.log 2>&1 < /dev/null &
```

## Watching the water (scene 8)

```bash
cd bin/SteamDeck/Release
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./DayScene --unattended --api vulkan \
  --scene 8 --backyard-start water --backyard-no-hud \
  --pbf-wind 8 45 --width 1600 --height 900 --logLevel info
```

- `--backyard-no-hud` hides the always-on quick bar and grab crosshair so the water fills the frame.
- `--pbf-wind <speed> <heading>` sets surface wind (heading in degrees from +X toward +Z).
- Other scene-8 knobs: `--pbf-iterations N`, `--pbf-substeps N`, `--pbf-radius m`,
  `--pbf-k <artificial pressure>`, `--pbf-visc <XSPH>`, `--pbf-gravity <g>`,
  `--pbf-maxvel <m/s>`, `--pbf-diagnostics <frames>`, `--pbf-overflow <m>`.
- `--backyard-start balls|water|lake` picks the initial condition.

## Observed results (2026-11-06, this host)

- Adapter: `[Vulkan] GPU: NVIDIA RTX PRO 6000 Blackwell Workstation Edition (vendor=0x10de device=0x2bb1)`
- 7 Backyard GLBs loaded; `[Backyard] Ready: 7 mesh instances`
- Dump: `RT dump complete -> dumps_vulkan_f60_20261106_192424/ (6 files)` (1280x720); plain run EXIT_CODE=0
- Selftest: `[BackyardSelfTest] PASS: api=vulkan  (BackyardSelfTest.cpp:1388)`; EXIT_CODE=0

## Caveats

- Vulkan only on Linux (no D3D/GL); T8ditor not built (`T850_BUILD_EDITOR=OFF`).
- Official Deck flow is `steamdeck/BuildSteamRuntime.sh` (Podman + steamrt/sniper) — not used here.
- Shader cache: `Assets/Shaders/.t8shadercache/`.
