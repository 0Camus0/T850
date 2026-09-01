---
name: t850-deck-performance
description: "Use when building, updating, deploying, registering, or running T850 on Steam Deck; profiling D3D11, D3D12, Vulkan, or OpenGL with PresentMon; comparing FPS/frame-time/CPU/GPU latency across APIs; interpreting 1% lows, bottlenecks, present modes, engine GPU scopes, or performance regressions."
argument-hint: "State Deck host/root or local APIs, scene, resolution, warmup/capture duration, and whether to deploy, capture, compare, or diagnose."
---

# T850 Steam Deck and Performance Workflow

Use controlled, matched workloads. Keep tools and captures outside the repository. Do not infer a bottleneck from FPS alone.

## Roots and Defaults

```powershell
$RepoRoot = git rev-parse --show-toplevel
$SourceRoot = Join-Path $RepoRoot 'T850'
$Runtime = Join-Path $SourceRoot 'bin\x64\Release'
$Exe = Join-Path $Runtime 'DayScene.exe'
```

Current scene indices:

```text
0 Sandbox
1 DayScene
2 Quake3Mock
3 RagdollEditor
4 SceneTemplate
5 VoxelScene
6 Minecraft
```

For comparable Windows captures, default to:

- Release x64;
- explicit API, scene, resolution, and culling mode;
- `--logLevel error` during external timing;
- a warmup before measurement;
- sequential captures from one unchanged executable;
- no trace logging, frame dumping, fixed timestep, telemetry, or internal profiler during the PresentMon run.

## Phase 1: Build and Deploy to Steam Deck

### Windows working-tree update

Use `UpdateSteamDeck.ps1` when local changes are not committed or pushed. Configure ignored `deckConfig.json` at the repository root:

```json
{
  "deckHost": "10.0.0.225",
  "deckUser": "deck",
  "deckRoot": "/home/deck/Code/T850-minecraft-atlas-test",
  "baseDeckRoot": "/home/deck/Code/T850"
}
```

Run from the repository root:

```powershell
# Sync HEAD plus local tracked/untracked overlays, build SteamRT, install shortcuts.
.\UpdateSteamDeck.ps1

# Also launch Minecraft fullscreen as a supervised user service.
.\UpdateSteamDeck.ps1 -Run

# Sync and refresh shortcuts without compiling.
.\UpdateSteamDeck.ps1 -SkipBuild

# Sync only.
.\UpdateSteamDeck.ps1 -SkipBuild -SkipLauncherInstall
```

The updater must:

- deploy into a managed root different from the base checkout;
- exclude `T850/Librerias/vcpkg` from transfer;
- reuse the base checkout's initialized vcpkg tree;
- run the official SteamRT build;
- invalidate stale SteamRT `.pch` files after synchronized headers change;
- never reset, clean, switch, or overwrite the user's base Deck checkout;
- preserve unrelated local and remote changes.

### Official SteamRT build on Linux/Deck

```bash
cd T850
./steamdeck/BuildSteamRuntime.sh --configuration Release
```

Expected outputs:

```text
bin/SteamDeck/Release/DayScene
bin/SteamDeck/Release/libc++.so.1
bin/SteamDeck/Release/libc++abi.so.1
bin/SteamDeck/Release/libunwind.so.1
```

Treat missing Podman/container/dependencies as environment failures, not source failures. Treat CMake/compiler/linker errors as source/build failures.

### Install and run

```bash
./steamdeck/InstallSteamDeckLauncher.sh
./steamdeck/T850.sh --game-mode --scene 6
./steamdeck/T850.sh --desktop --scene 6
```

The installer adds **T850**, **T850 Minecraft**, and **T850 Launcher** to Steam shortcuts. It backs up `shortcuts.vdf` as `shortcuts.vdf.t850bak`. Restart Steam or switch modes after shortcut writes so Steam reloads the database.

For a persistent SSH-launched test, use a transient user service rather than `nohup`:

```bash
systemd-run --user --unit=t850-minecraft --collect \
  --property="WorkingDirectory=$ROOT/T850" \
  --setenv=XDG_RUNTIME_DIR=/run/user/1000 \
  --setenv=WAYLAND_DISPLAY=wayland-0 \
  --setenv=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
  --setenv=SDL_VIDEODRIVER=wayland \
  "$ROOT/T850/steamdeck/T850.sh" --game-mode --scene 6 --log-level info
```

Deck runtime acceptance requires:

- process remains active or finite capture exits 0;
- Vulkan reports RADV/VANGOGH on Deck;
- authored scene and world initialize, and the Minecraft mob reports a ready voxel path;
- a Minecraft Recast NavMesh is expected only when the diagnostic overlay is authored on;
- SDL opens `Steam Deck Controller`;
- scene logs its gamepad controls active;
- no `[ERROR]`, fatal, validation, device-loss, OOM, or missing required asset line;
- expected runtime libraries and frame dump exist.

## Phase 2: Capture with PresentMon

### Install outside Git

Use the official `GameTechDev/PresentMon` portable release under a user tool directory, for example:

```text
%LOCALAPPDATA%\T850Tools\PresentMon\<version>\PresentMon-<version>-x64.exe
```

Verify before use:

```powershell
Get-FileHash $PresentMon -Algorithm SHA256
Get-AuthenticodeSignature $PresentMon | Format-List Status,SignerCertificate
& $PresentMon --help
```

Require a valid signature. Never place the executable or captures in the repository.

### Controlled capture

Use an external output root such as:

```powershell
$ProfileRoot = Join-Path $env:LOCALAPPDATA 'T850Profiles\api-comparison'
```

For each API, ensure no pre-existing `DayScene` process, launch from the Release output directory, target the exact PID, and clean up only the process created by the capture:

```powershell
$Api = 'd3d12' # repeat with vulkan
$Out = Join-Path $ProfileRoot $Api
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$App = Start-Process -FilePath $Exe -WorkingDirectory $Runtime -PassThru `
  -ArgumentList @(
    '--api', $Api,
    '--scene', '6',
    '--width', '1920', '--height', '1080',
    '--culling', 'full',
    '--logLevel', 'error'
  ) `
  -RedirectStandardOutput (Join-Path $Out 'dayscene.stdout.txt') `
  -RedirectStandardError (Join-Path $Out 'dayscene.stderr.txt')

try {
  & $PresentMon `
    --process_id $App.Id `
    --delay 15 `
    --timed 20 `
    --terminate_after_timed `
    --stop_existing_session `
    --no_console_stats `
    --v2_metrics `
    --output_file (Join-Path $Out 'presentmon.csv')
} finally {
  if (-not $App.HasExited) { Stop-Process -Id $App.Id -Force }
  $App.WaitForExit()
}
```

Do not use `$PID` as a PowerShell variable; it is reserved. Use `$App.Id` or `$appProcessId`.

If PresentMon requests elevation or returns access denied, report the privilege blocker. Do not trigger UAC automatically.

Capture acceptance requires:

- PresentMon exit 0;
- CSV contains only the target process/PID and expected swapchain;
- measured CPUStartTime span matches requested duration;
- engine logs contain no error/device-loss/validation lines;
- same executable, scene, assets, resolution, culling, warmup, duration, window mode, and foreground conditions for every API.

### Engine profiler attribution

Run separately from PresentMon because profiling/logging changes timing:

```powershell
Set-Location $Runtime
.\DayScene.exe `
  --api d3d12 `
  --scene 6 `
  --width 1920 --height 1080 `
  --culling full `
  --logLevel info `
  --profile --profileFrames 2000
```

Repeat for Vulkan. The profiler reports named GPU/CPU scopes, draw calls, and triangles. Current `_exit()` shutdown can truncate the last report rows; treat missing backend-only rows as unavailable, not zero.

## Phase 3: Assess Performance Metrics

### Core calculations

For $N$ rows with first/last `CPUStartTime` in milliseconds:

$$
\text{effective FPS} = \frac{(N-1)\,1000}{t_{last}-t_{first}}
$$

PresentMon `FrameTime` percentiles are frame-time percentiles. Convert tails to conventional lows:

$$
\text{1\% low FPS} = \frac{1000}{P99(\text{FrameTime})}
$$

$$
\text{0.1\% low FPS} = \frac{1000}{P99.9(\text{FrameTime})}
$$

Report at minimum:

- effective FPS;
- FrameTime mean, median, P95, P99, P99.9, max;
- 1% and 0.1% low FPS;
- CPUBusy and CPUWait mean/P95/P99;
- GPUTime, GPUBusy, GPUWait mean/P95/P99;
- GPULatency and DisplayLatency mean/P95/P99 plus missing count;
- PresentRuntime, PresentMode, SyncInterval, AllowsTearing, flags, swapchain count;
- row count and measured span.

### Bottleneck interpretation

Use multiple signals:

| Evidence | Interpretation |
|---|---|
| `GPUTime ≈ FrameTime`, `GPUBusy ≈ FrameTime`, GPUWait near zero | GPU queue is continuously busy; likely GPU-throughput limited or tightly balanced. |
| CPUBusy exceeds GPUTime and GPU has gaps/wait | CPU submission/game/update likely limits throughput. |
| CPU fence wait is large while GPU is continuously busy | GPU backpressure, not proof of expensive CPU work. |
| Frame time changes nearly linearly with pixel count | Pixel/fill/bandwidth/fullscreen-pass pressure. |
| Frame time barely changes with resolution but tracks draw count | CPU submission, vertex/geometry, or fixed overhead. |
| High CPUWait with stable present interval | Pacing, synchronization, VSync/frame cap, or compositor wait. |
| Spikes coincide with chunk generation/uploads/nav rebuild | Streaming transient, distinct from steady-state throughput. |

Do not classify a run as CPU-bound solely because `corr(FrameTime, CPUBusy)` is high. PresentMon's CPU interval can follow frame pacing, while the GPU remains continuously saturated. Compare absolute CPU/GPU durations, waits/gaps, resolution scaling, and engine fence scopes.

Do not compare DisplayLatency across incompatible present paths without qualification. D3D12 may use `Hardware Composed: Independent Flip` with tearing while Vulkan can appear as `Composed: Copy with GPU GDI`; missing Vulkan display samples and compositor behavior affect display latency but do not directly explain render-pass GPU time.

### Engine scope interpretation

Normalize cumulative draws and triangles by each scope's sample count. Compare:

- GBuffer GPU time and CPU submission;
- each shadow cascade;
- shadow accumulation and blur;
- deferred lighting;
- DOF/bloom/postprocessing;
- backend fence/reset/submit/present CPU scopes when present.

Do not blindly sum nested or overlapping scopes. Sum only known sequential, non-overlapping render-graph pass scopes, and compare that total against PresentMon GPUTime to estimate uninstrumented barriers, transitions, command work, overlays, and present overhead.

If APIs render different draw counts, call that out before attributing timing differences to backend efficiency. Use deterministic frame comparison to prove visual/workload parity when needed (`t850-api-frame-comparison`).

### Resolution scaling

Use at least three resolutions when deciding fixed versus pixel cost. Fit:

$$
t_{frame} = a + b \times \text{megapixels}
$$

- $a$: fixed CPU/scene/submission cost estimate;
- $b$: pixel-dependent rendering cost estimate.

Use feature A/B captures only after establishing a baseline. Change one feature at a time, preserve all other settings, and account for shared render-graph passes.

## Completion Report

Report:

1. exact executable/configuration, API, scene, resolution, culling, warmup, duration;
2. PresentMon version/path/signature status and external CSV paths;
3. matched metric table and API deltas;
4. present-mode differences and missing metrics;
5. engine profiler pass deltas, normalized draw/triangle differences, and truncated rows;
6. bottleneck conclusion with evidence and uncertainty;
7. Deck build/deploy/run outputs when applicable;
8. tests, errors, unavailable evidence, and unrelated worktree state left untouched.

Related skills:

- `t850-platform-deploy`
- `t850-api-frame-comparison`
- `t850-local-build-validation`

Related documentation:

- `documentation/platform/steam-deck.md`
- `documentation/debug/diagnostics.md`
- `documentation/development/runtime-configuration.md`
- `documentation/testing/verification.md`
