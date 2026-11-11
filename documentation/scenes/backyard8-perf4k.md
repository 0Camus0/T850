# Backyard 8: 4K performance (WO-P0 through WO-P4)

Status: all work orders P0-P4 landed on `backyard8-perf4k`; the numbers below are Claude's
final guarded measurements of 2026-11-11, from
[backyard8-perf4k-work-orders.md](backyard8-perf4k-work-orders.md) §"Claude's final
measurements". Pipeline reference:
[backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md).

## What was measured

Setup: RTX PRO 6000 Blackwell, Linux, Vulkan, 3840x2160, screen-space renderer.

- Before: `a960d75`, whose fluid GPU path is the original code. It includes WO-P4 (CPU only)
  and the WO-P2a marker (no effect).
- After: HEAD `f8efec4` plus status commits (WO-P1, WO-P2a/b, WO-P3, WO-P4).
- Close view: `--backyard-camera 0 1.3 -10.0 0.32 0`.

GPU per pass (`--benchmarkFixedDt`, `--profile` 400 frames, second of two runs; builds
alternated; r1 and r2 within 0.5 %). Milliseconds, before → after:

| View, start | Fluid Splat | Fluid Surface Shade | Render graph total |
|---|---|---|---|
| Default, water | 2.40 → 0.92 | 0.97 → 0.94 | 6.12 → 4.65 (-24 %) |
| Default, lake | 1.61 → 0.76 | 0.70 → 0.69 | 4.83 → 3.99 (-17 %) |
| Close, water | 5.60 → 2.44 | 1.60 → 1.35 | 10.09 → 6.78 (-33 %) |
| Close, lake | 4.25 → 2.29 | 1.26 → 1.22 | 8.20 → 6.26 (-24 %) |

Real time (HUD on, no fixed dt, 840 frames after 60 warm-up):

| View, start | Before | After |
|---|---|---|
| Default, water | 140 fps (7.15 ms mean, p99 12.4) | 174 fps (5.74 ms, p99 10.6) |
| Default, lake | 172 fps (5.82 ms, p99 9.0) | 205 fps (4.88 ms, p99 7.6) |
| Close, water | 88 fps (11.36 ms, p99 16.2) | 124 fps (8.08 ms, p99 12.6) |
| Close, lake | 103 fps (9.66 ms, p99 13.1) | 134 fps (7.46 ms, p99 10.1) |

CPU (WO-P4): Deferred Light Volumes 0.80-0.91 → 0.21-0.29 ms per frame.

## Why 60 fps was hard: the GPU was shared

qwen-local generates on this GPU (`tensorfold-native`) almost all the time it works. With
qwen-local generating on the same GPU, the default/water view fell from 144 to 46 fps
(22 ms mean, p99 40 ms) before these changes. A frame rate seen while the agents work is not
the game's frame rate; every measurement in this document is taken with the GPU otherwise
idle.

How to measure cleanly:

- Check with `nvidia-smi pmon -c 1 -s u`: any process besides DayScene, typically
  `tensorfold-nati`, must show `sm` below 5.
- `backyard8_perf.py` (WO-P0) enforces this: before each run it waits until
  `nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits` reads below 10 for
  5 consecutive samples 2 s apart (give up after 15 minutes); during the run it samples
  `nvidia-smi pmon -c 1 -s u` every 2 s and repeats a contended run up to 3 times.
- `--no-guard` skips the guard for functional checks; its output is labelled UNGUARDED and is
  not valid for comparison.

## What changed

### WO-P0: measurement tooling (`dd412a8`)

- `--backyard-camera <x> <y> <z> <pitch> <yaw>` flag: overrides the authored camera at load
  time, applied in `BackyardScene::InitVars` right after `m_camera = *m_sceneSetup.GetCamera()`
  and before `AttachCamera`. Malformed values log an error and are ignored.
- `T850/scripts/validation/backyard8_perf.py`: GPU mode (per-pass GPU ms table), real-time
  mode (frame statistics and CPU scope means), and the contention guard described above.
- No GPU change; the tooling is what makes the other work orders measurable.

### WO-P1: thickness and whitewater on a coarser grid (`c960808`)

The splat's thickness and whitewater scatter was the largest single cost (3.7 of 5.4 ms
close up, 2.3 ms of that in the four whitewater atomics). The resolve blurs the thickness
sum with a 5x5 binomial anyway, so a coarser grid loses nothing visible.

- New setting `screenSpace.thicknessDownsample` (JSON key `thicknessDownsample`, int 0..2,
  default 0): 0 = auto (half the depth grid when the window height is at least 1440, else the
  depth grid), 1 = the depth grid, 2 = half the depth grid. It is resolved every frame, so
  resizing the window switches it without a graph rebuild. The sphere renderer (the splat
  kernel's `!surface` branch) always uses 1.
- One constant, `ThicknessShift` (`s`, 0 or 1), reaches `CS_FluidSplat`, `CS_FluidClear`,
  `CS_FluidResolve` and `CS_ParticleShade` through their constants builders.
- Coarse grid: `Wt = (OutputWidth + (1 << s) - 1) >> s`, `Ht` likewise. `CS_FluidSplat`
  splats thickness/whitewater on the coarse grid (centre and radius divided by `2^s`, the
  64-px cap applied before the division); `CS_FluidResolve` upsamples the coarse sums to the
  depth grid in the same pass; `CS_FluidClear` clears the first `Wt * Ht` thickness entries
  and `4 * Wt * Ht` aux entries; `CS_ParticleShade` reads the aux channels on the coarse
  grid.
- Measured effect: most of the splat gain, about 2.8 ms at the close view.

### WO-P2a: buried marker in the anisotropy buffer (`2ff6ea2`)

`CS_PbfAnisotropy` already has each particle's neighbour count and the weighted mean offset
of its neighbours. A particle is buried when its neighbourhood is full and balanced:
`neighbours >= 24` and `length(mean) <= 0.1 * h`. The marker is stored in
`Anisotropy[i * 3 + 2].z` as `-neighbours`, positive otherwise. No new buffer; the marker
has no effect on its own (no effect in the before build).

### WO-P2b: cap-aware buried skip (`d1bc742`, follow-up `f8efec4`)

`CS_FluidSplat` skips buried particles in the ellipsoid depth loop and the surfel pass,
before the rectangle loop. The thickness/whitewater loop still includes them, so coverage
and the metre-of-water sum do not change.

The first version skipped everywhere and punched rectangular holes near the camera: the
depth rectangle is capped at 64 half-res pixels, and a surface ellipsoid 2 m away projects
to about 140 px in 4K, so the cap cut it and the overlapping interior particles that used
to fill those cuts were the ones being skipped. The follow-up (`f8efec4`) computes the
uncapped projected bound first and applies the skip only when `bPxRaw < 48`, where the
cap does not bite. Measured effect: about 0.24 ms default, 0.06 ms close.

### WO-P3: shade: per-frame sky term and Fresnel-gated SSR (`55ea7ed`, follow-up `be39443`)

- `SkyIrradiance()` gives the same value for every pixel. Thread 0 of each 8x8 group
  computes it into `groupshared float3`, followed by `GroupMemoryBarrierWithGroupSync()`;
  every thread reaches the barrier (the `Mode == 0` and bounds returns are moved below it).
  Measured worth (Claude's ablation): about 0.17 ms.
- Trace SSR only where it can be seen: the Fresnel term is computed before
  `TraceReflection`; `ssrWeight = smoothstep(F0, F1, fresnel)`; when `ssrWeight == 0` the
  trace is skipped and the environment is used. The first commit's gate was inert:
  `fresnel = 0.02 + 0.98 * pow(1 - cos, 5)` never drops below 0.02, so `F0 = 0.0 /
  F1 = 0.10` left `smoothstep` at least 0.104 and the trace always ran. The follow-up
  (`be39443`) sets F0 = 0.03, F1 = 0.06 (with power 5, Fresnel 0.03 is about 53 degrees of
  incidence and 0.06 about 62), so the gate is active. Measured effect: about 0.25 ms of
  shade at the close view.

### WO-P4: tiled deferred lights: CPU work and uploads proportional to the lights in use
(`4389a36`, follow-up `6961c93`)

At 4K the tiled-light path in `RenderQuad.cpp` cleared a 16.7 MB index array with
`std::fill` every frame and re-uploaded it whole through a new staging buffer whenever any
tile assignment changed (every frame the camera moved). The shader reads only the first
`count` entries of a tile, so:

- Clear only the headers and counts, not the index array.
- Size the index texture's width to the largest per-tile count in use, rounded up to 8 and
  capped at 128. Recreate the texture only when it must grow (and whenever the tile count
  changes, per the follow-up).
- Upload only that width.

CPU only; no GPU change. Measured effect: Deferred Light Volumes 0.80-0.91 → 0.21-0.29 ms
per frame.

## The new setting

`screenSpace.thicknessDownsample` in `Assets/Scenes/Backyard8Fluid.json` (JSON key
`thicknessDownsample`, int 0..2, JSON default 0):

- 0 = auto: half the depth grid when the window height is at least 1440, else the depth
  grid. Resolved every frame in the constants builders, so resizing the window switches it
  without a graph rebuild.
- 1 = the depth grid (today's behaviour).
- 2 = half the depth grid on each axis.

The sphere renderer (the splat kernel's `!surface` branch) always uses 1.

Dev GUI: a "thickness downsample" combo in the Screen Space panel, with the options
"auto (half on 1440p+)" / "full (depth grid)" / "half (half grid)".

## Measuring it yourself

`T850/scripts/validation/backyard8_perf.py` (WO-P0). Run from `T850/`:

```
cd T850
python3 scripts/validation/backyard8_perf.py
```

- **GPU mode** (default): per view/start cell, runs
  `DayScene --unattended --api vulkan --scene 8 --backyard-no-hud --width W --height H
  --postProcessMode compute --benchmarkFixedDt 0.0166667 --profile --profileFrames 400
  --logLevel info --logFile <tmp>`, plus `--backyard-start water` for the water start and
  `--backyard-camera 0 1.3 -10.0 0.32 0` for the close view. Parses the
  `[Profiler] <pass> CPU=<c>ms GPU=<g>ms` lines and prints one row per pass (passes at or
  above 0.03 ms in any column) with a sum row.
- **Real-time mode** (`--realtime`): HUD on (drop `--backyard-no-hud`), no fixed dt, and
  `--profile --profileCpuOnly --profileFrames 900 --telemetry
  --telemetryFrequencyFrames 1 --telemetryOutput <tmp>.json`. Skips the first 60 frames;
  reports the mean, median, p90 and p99 of `deltaMs`, and fps = 1000 / mean. Also reports
  the mean CPU ms of `gpu.gpu_wait`, `gpu.encode` and
  `render.pass.Deferred Light Volumes`.
- **Contention guard** (every mode, every run): before each run, wait until
  `nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits` reads below 10 for
  5 consecutive samples 2 s apart (give up after 15 minutes). During the run, sample
  `nvidia-smi pmon -c 1 -s u` every 2 s; if any process other than DayScene shows `sm` at
  5 or more, mark the run contended and repeat it, up to 3 attempts. Cells that never got a
  clean run are marked `contended`.
- **`--no-guard`**: skip the contention guard (no idle wait, no retries); the output is
  labelled UNGUARDED and is not valid for comparison.
- **Options**: `--views` (comma-separated: default,close; default both), `--starts`
  (comma-separated: water,lake; default both), `--width` / `--height` (default 3840x2160),
  `--runs` (default 2; the last run is reported), `--output <json>` (write the results as
  JSON to this path), `--day-scene <path>` (path to DayScene; default
  `T850/bin/SteamDeck/Release/DayScene`).

Examples:

```
python3 scripts/validation/backyard8_perf.py
python3 scripts/validation/backyard8_perf.py --realtime --views default --starts water
python3 scripts/validation/backyard8_perf.py --width 1920 --height 1080 --runs 3 --output perf.json
```

The script sets `SDL_VIDEO_DRIVER=x11` and `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` on
Linux and runs DayScene from its own directory.

## Point lights come back in static views

WO-P4's upload fix restored the point lights in static views. Until now, static views
(every dump, gate run and capture) rendered without the five point lights, which appeared
only once the camera moved.

The cause: when the tiled-light textures were created, the frame's freshly built light lists
were zeroed before the upload, and the assignments were then recorded as uploaded. With a
static camera the assignments never changed, so nothing re-uploaded them.

A static-view dump now shows the patio point lights, which is why pre-P4 captures differ. In
the Deferred target, 4,219 pixels got brighter by more than 4 levels and none got darker,
centred on the patio around the point light at (0, 1.6, 4.6).

## What is left

Remaining costs at 4K, close/water after:

- Fluid Splat 2.4 ms: the near-camera ellipsoids still splat every particle, because of
  the 64-px cap.
- Fluid Surface Shade 1.35 ms.
- Shadow Accumulation about 1.0 ms.
- GBuffer about 0.7 ms.
- The PBF step, about 2 ms per 1/60 s step, outside the profiled passes.

## Later candidates

From the work orders document:

- Shadow Accumulation (0.7-1.0 ms at 4K) runs full-resolution PCF with manual point taps
  plus SSAO in `FS_Quad.hlsl`, shared by every scene. Possible changes: half-resolution
  SSAO, and hardware comparison samplers for PCF. These are engine-wide and need Windows
  validation.
- SSR on a hierarchical (min-depth) pyramid instead of an 80-step linear march.
- A profiler scope for the PBF solver. The profiler frame starts in the draw phase
  (`Application.cpp:900`), after the update that records the solver, so a scope in the step
  is outside it today.

## Commit map

| WO | Commits |
|---|---|
| P0 | `dd412a8` |
| P1 | `c960808` |
| P2 | `2ff6ea2` (P2a), `d1bc742` (P2b), `f8efec4` (P2b follow-up) |
| P3 | `55ea7ed`, `be39443` (follow-up) |
| P4 | `4389a36`, `6961c93` (follow-up) |
