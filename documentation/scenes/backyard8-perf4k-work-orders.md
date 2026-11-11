# Backyard 8: 4K performance work orders

Branch `backyard8-perf4k`, from `b376c0d` (the head of `backyard8-surfels`). Agents implement.
Claude reviews each commit, re-measures with the GPU otherwise idle, and signs off.

## Why the game "barely does 60 fps at 4K"

Claude measured on this machine: RTX PRO 6000 Blackwell, Linux, Vulkan, IMMEDIATE present,
screen-space renderer, 3840x2160, HUD on, real time.

| View | GPU otherwise idle | While qwen-local generates |
|---|---|---|
| Default camera, water start | 144 fps (6.9 ms mean, p99 12.6 ms) | **46 fps** (22.0 ms mean, p99 39.6 ms) |
| Default camera, lake start | 176 fps (5.7 ms) | not run |
| Close-up (`0 1.3 -10.0`, pitch 0.32), water start | 91 fps (11.0 ms, p99 16.2 ms) | not run |
| Close-up, lake start | 105 fps (9.5 ms, p99 13.7 ms) | not run |

1. **qwen-local runs on the same GPU** (`tensorfold-native`, 127.0.0.1:8089). While it
   generates, the GPU sits at 98 % utilisation and its 450 W limit, and the game gets about a
   third of it. A frame rate seen while the agents work is not the game's frame rate.
   **Every measurement in this work is taken with qwen-local idle** (see Rules).
2. With the GPU to itself the game is GPU-bound at 4K. The CPU waits 4-8 ms per frame on the
   frame fence in `BeginFrame` (`gpu.gpu_wait`). Telemetry attributes the wait to
   `scene.update` because the PBF step opens the frame.

GPU time per pass at 4K, in ms (`--profile`, 400 frames, fixed dt):

| Pass | Default, water | Default, lake | Close, water | Close, lake |
|---|---|---|---|---|
| Fluid Splat | 2.09 | 1.48 | **5.47** | 4.08 |
| Fluid Surface Shade | 0.90 | 0.70 | 1.36 | 1.26 |
| Shadow Accumulation | 0.73 | 0.73 | 1.01 | 1.01 |
| GBuffer | 0.75 | 0.75 | 0.67 | 0.67 |
| All other passes | 1.0 | 0.9 | 1.0 | 0.9 |
| Render graph total | 5.48 | 4.51 | 9.52 | 7.89 |

The PBF solver runs outside the profiled passes, at about 2 ms of GPU time per 1/60 s step
(estimated).

Ablations (close-up, water start, 4K). Claude edited scratch copies of the kernels; nothing is
committed. A dash means the column was not measured.

| Change | Fluid Splat (ms) | Shade (ms) |
|---|---|---|
| None | 5.43 | 1.36 |
| No thickness/whitewater loop | 1.71 | - |
| No depth loop | 3.66 | - |
| Thickness loop on every 2nd pixel in x and y (about the cost of a half-size grid) | 2.59 | - |
| No whitewater aux atomics | 3.14 | - |
| Plain read before `InterlockedMin` (what the depth-loop comment claims the code does) | 6.92, slower | - |
| No SSR (`TraceReflection` returns at once) | - | 0.69 |
| No refraction march | - | 1.02 |
| `SkyIrradiance()` replaced by a constant | - | 1.19 |
| No foam | - | 1.36 |
| No ripples | - | 1.99, slower: smooth normals send SSR rays further |

CPU: `render.pass.Deferred Light Volumes` takes 1.22 ms of the 2.3 ms encode per frame at 4K.
The tiled-light path in `RenderQuad.cpp` works like this at 4K:

- The index array covers 8,160 tiles (32 px) x 128 lights x RGBA32F = 16.7 MB.
- It is cleared with `std::fill` every frame, although the scene has 6 lights.
- It is re-uploaded whole, through a new 16.7 MB staging buffer, whenever any tile
  assignment changes, which is every frame the camera moves.

What follows from the numbers:

- The splat's thickness and whitewater scatter is the largest single cost: 3.7 of 5.4 ms close
  up, 2.3 ms of that in the four whitewater atomics. The resolve blurs the thickness sum with a
  5x5 binomial anyway, so a coarser grid loses nothing visible.
- The depth scatter (1.8 ms) splats every particle. Only the top layer or two can be the front
  surface, and a calm pool is about 11 particle layers deep.
- SSR is half of the shade. Where the Fresnel weight is a few percent, the trace hardly
  changes the pixel.
- Expected after WO-P1 to WO-P4:
  - close-up at 4K: about 6-7 ms per frame (about 150 fps);
  - default view: about 4-5 ms;
  - CPU: 1.1 ms per frame less, and no 16.7 MB uploads while the camera moves.

## Rules (every work order)

- **Never `git stash`, `git checkout -- <path>`, `git restore`, `git reset` or `git clean` in this
  working tree.** Other agents and Claude have uncommitted edits in it. To compare with an
  older commit, use `git worktree add /tmp/<name> <commit>` and remove it afterwards.
- Touch only the files your work order owns. Commit only them, with a pathspec:
  `git commit -m "WO-Pn: ..." -- <paths>`. Do not stage or commit anybody else's files. Do not
  amend or rebase. Never push.
- The commit subject starts with `WO-Pn:`. The body lists what you ran and the exact results.
- Build: `cd T850 && cmake --build build/linux --target DayScene -j$(nproc)`. Shaders compile at
  runtime, so `.hlsl` edits need no rebuild.
- Run from `T850/bin/SteamDeck/Release` with
  `SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu`.
  - Always pass `--unattended --scene 8`. Without `--scene 8` the app starts its default scene
    on the user's desktop.
  - `--backyard-start water` selects the splash start; no flag gives the lake start.
  - `--dump-frame N` writes the render targets at frame N and exits.
  - Delete the `dumps_*` folders you create.
- **Timing.** Use `--benchmarkFixedDt 0.0166667`, which runs unpaced. Never time with
  `--regressionFixedDt`: it sleeps to 60 Hz between frames.
  - Take two runs and report the second.
  - **qwen-local shares this GPU.** A run is valid only if no other process used the GPU during
    it. Check with `nvidia-smi pmon -c 1 -s u`: any process besides DayScene, typically
    `tensorfold-nati`, must show `sm` below 5. `backyard8_perf.py` (WO-P0) enforces this. Never
    compare a contended run.
- **The look does not change** unless a work order says so.
  - The full gate `python3 T850/scripts/validation/backyard8_regression.py` must PASS without
    re-freezing.
  - Each work order also reports `RT_Dump_HDR_Final.ppm` differences at 4K: mean absolute
    difference per channel and the 99th percentile, for the default and close views. Claude
    views the dumps.
- Static shader check, for each changed `.hlsl`:
  `glslangValidator -D -V -S comp -e CS --target-env vulkan1.0 -DT850_VULKAN=1 -DT850_SPIRV=1 <file> -o /tmp/x.spv && spirv-val --target-env vulkan1.0 /tmp/x.spv`.
- GL twins are generated, never hand-edited:
  `cd T850 && SPIRV_CROSS=$HOME/src/SPIRV-Cross/build/spirv-cross python3 scripts/shaders/generate_gl_compute_twins.py <Name>.hlsl`
  (the file name with `.hlsl`). Then `python3 scripts/validation/check_gl_twins.py`.
- Validators: `python3 T850/scripts/validation/check_fluid_constants.py` must exit 0. Check the
  exit code itself, not through a pipe. `./DayScene --game-selftest` must give 78 PASS.
- Never re-freeze `T850/scripts/validation/backyard8_references.json`. Claude does it after a
  visual sign-off.
- Report numbers, not adjectives. Distinguish pass, fail and "not run".
- When blocked, or when a design detail here is wrong, append to "Requests to Claude" at the
  end and continue with what you can.

## Order

| Work order | Starts | Reason |
|---|---|---|
| WO-P0 and WO-P1 | Together | Their files do not overlap |
| WO-P4 | After WO-P0, on the same worker | |
| WO-P2 and WO-P3 | Together, after WO-P1 is committed | Both touch kernels WO-P1 changes; disjoint from each other |
| WO-P5 | Last, after Claude's final measurements in "Notes from Claude" | |

Two work orders never edit the same file at the same time.

## WO-P0: measurement tooling

Owns:

- `T850/DayScene/App.cpp`
- `T850/DayScene/BackyardScene.cpp`, `T850/DayScene/BackyardScene.h`
- `T850/Framework/src/utils/ConfigRuntime.cpp` (help text only, if that is where the
  `--backyard-*` flags are listed)
- `T850/scripts/validation/backyard8_perf.py` (new)

1. **`--backyard-camera <x> <y> <z> <pitch> <yaw>`** takes five floats.
   - Store them through a static setter on BackyardScene, like `SetSpawnOnStart`.
   - In BackyardScene, right after `m_camera = *m_sceneSetup.GetCamera();` and before
     `AttachCamera`, apply them:
     `m_camera.Eye = XVECTOR3(x, y, z); m_camera.Pitch = pitch; m_camera.Yaw = yaw; m_camera.Update(0.0f);`
   - With malformed values, log an error and ignore the flag.
   - Add a help line next to the other `--backyard-*` flags.
   - Checks:
     - `--backyard-camera 0 1.3 -10.0 0.32 0 --backyard-start water --dump-frame 150` at
       960x540 must show water on more than 50 % of the pixels. Report the fraction from the
       fluid dump.
     - With the flag set to the scene camera (`0 2.4 -13.5 0.22 0`), the dump must equal a run
       without the flag.
2. **`T850/scripts/validation/backyard8_perf.py`**
   - Views: `default` (no flag) and `close` (`--backyard-camera 0 1.3 -10.0 0.32 0`).
   - Starts: `water` and `lake`.
   - Options: `--views`, `--starts`, `--width`/`--height` (default 3840x2160), `--runs`
     (default 2; report the last run), `--output <json>`.
   - **GPU mode** (default):
     - Command: `DayScene --unattended --api vulkan --scene 8 --backyard-no-hud --width W --height H --postProcessMode compute --benchmarkFixedDt 0.0166667 --profile --profileFrames 400 --logLevel info --logFile <tmp>`,
       plus `--backyard-start water` for the water start.
     - Parse the `[Profiler] <pass> CPU=<c>ms GPU=<g>ms` lines.
     - Print one row per pass with GPU ms in each view/start column. Keep passes at or above
       0.03 ms in any column, and add a sum row.
   - **Real-time mode** (`--realtime`):
     - HUD on (drop `--backyard-no-hud`), no fixed dt, and
       `--profile --profileCpuOnly --profileFrames 900 --telemetry --telemetryFrequencyFrames 1 --telemetryOutput <tmp>.json`.
     - Skip the first 60 frames.
     - Report the mean, median, p90 and p99 of `deltaMs`, and fps = 1000 / mean.
     - Also report the mean CPU ms of `gpu.gpu_wait`, `gpu.encode` and
       `render.pass.Deferred Light Volumes`.
   - **Contention guard** (every mode):
     - Before each run, wait until
       `nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits` reads below 10
       for 5 consecutive samples 2 s apart. Give up after 15 minutes.
     - During the run, sample `nvidia-smi pmon -c 1 -s u` every 2 s. If any process other
       than DayScene shows `sm` at 5 or more, mark the run `contended` and repeat it, up to 3
       attempts.
     - Print `contended` for each cell that never got a clean run.
   - Run from `T850/bin/SteamDeck/Release` with the two environment variables. Delete the
     temporary logs. Exit 0 on success.
   - Claude's scratch scripts show the parsing: `/tmp/perf4k/passes.py`,
     `/tmp/perf4k/frames.py`, `/tmp/perf4k/run.sh`. Read them; do not edit them.
   - Check: paste the 4-cell GPU table and the real-time numbers for `default`/`water` in the
     commit body. They should be within about 10 % of the tables above.

## WO-P1: thickness and whitewater on a coarser grid (largest gain)

Owns:

- `CS_FluidSplat`, `CS_FluidResolve`, `CS_FluidClear` and `CS_ParticleShade`, each `.hlsl`
  plus its `.glsl` twin. In CS_ParticleShade, only the aux loads.
- `ComputeKernelRegistry.cpp`, `compute_kernels.json`
- `ParticleSystem.h/.cpp`
- `Backyard8FluidJson.h`, `Backyard8Scene.cpp/.h`, `Backyard8Fluid.json`
- `check_fluid_constants.py`, only if its expectations must change

Design:

- **New setting `screenSpace.thicknessDownsample`** (JSON key `thicknessDownsample`, int 0..2,
  default 0):
  - 0 = auto: 2 when the window height is at least 1440, else 1. Resolve it every frame, so
    resizing the window switches it without a graph rebuild.
  - 1 = the thickness and whitewater sums use the fluid depth grid (today's behaviour).
  - 2 = half the fluid depth grid on each axis.
  - Add a GUI combo (auto / full / half) in the screen-space section.
  - The sphere renderer (the splat kernel's `!surface` branch) always uses 1.
- **One constant, `ThicknessShift`** (`s`, 0 or 1), reaches CS_FluidSplat, CS_FluidClear,
  CS_FluidResolve and CS_ParticleShade through their constants builders.
  - Prefer a free slot.
  - A cbuffer may grow (`D3D12Compute.cpp` binds an over-budget constant block as a root
    CBV), but then update its `constant_count` in `compute_kernels.json`.
  - Keep `check_fluid_constants.py` green.
- **Coarse grid.** `Wt = (OutputWidth + (1 << s) - 1) >> s`, and `Ht` likewise.
  `OutputWidth` and `OutputHeight` are the depth grid (`$window_half`).
  - `ThicknessBits` and `AuxBits` keep their allocation.
  - Only the first `Wt * Ht` entries are used, at index `x + y * Wt`; aux uses `4 *` that.
- **CS_FluidSplat**, thickness/whitewater loop only (the depth and surfel loops do not change):
  - Centre and radius on the coarse grid are `centre / 2^s` and `rPx / 2^s`. The 64-px cap
    applies on the depth grid, before the division.
  - A particle that passes today's `rPx >= 0.5` test splats with a coarse radius of at least
    0.75 texel. Distant water must keep its chords.
  - The chord value per texel keeps today's formula: `h` comes from the coarse disc.
- **CS_FluidResolve** upsamples the coarse sums to the depth grid in the same pass.
  `FluidThickness` keeps its size and meaning, so `SampleFluid` in the shade does not change.
  - With `s = 1`, read the coarse grid at `((x + 0.5) / 2 - 0.5, (y + 0.5) / 2 - 0.5)` with a
    separable 4-tap kernel per axis: bilinear weights convolved with the `[1 2 1] / 4`
    binomial. That roughly matches today's 5x5 binomial on the depth grid.
  - With `s = 0`, keep today's 5x5 exactly.
  - Coverage stays `smoothstep(0, edgeThickness, thickness)`, gated by the depth texel's
    `z > 0`.
- **CS_FluidClear** clears the first `Wt * Ht` thickness entries and `4 * Wt * Ht` aux entries.
  Report its time.
- **CS_ParticleShade**: `LoadAux`/`LoadAuxTexel` read `AuxBits` on the coarse grid.
  - Map the pixel to the coarse grid the same way the depth grid is mapped, using the coarse
    size.
  - Keep the 4-tap tent.
- Regenerate the GL twins of the four kernels.

Acceptance:

- Default config at 960x540 (auto resolves to 1):
  - the full gate PASSes;
  - HDR_Final matches the pre-change dumps (mean absolute difference below 0.1 level).
- At 4K, `thicknessDownsample: 2` against `1`: report the HDR_Final mean absolute difference
  and the 99th percentile. Cells: default and close views, water start at f150 and lake start
  at f300. Target: below 1 level mean, 99th percentile at or below 4 levels.
- Perf (`backyard8_perf.py`, 4K, GPU mode):
  - Fluid Splat close/water at or below 2.8 ms (from 5.4);
  - default/water at or below 1.2 ms;
  - Resolve and Clear grow by at most 0.05 ms each.
- Self-test 78 PASS; the constants and twin validators exit 0.

## WO-P2: skip buried particles in the depth and surfel scatter

Owns:

- `CS_PbfAnisotropy.hlsl/.glsl`
- `CS_FluidSplat.hlsl/.glsl`: the depth and surfel branches only, after WO-P1 is committed
- any kernel that reads the anisotropy component you change. Grep first and list them in the
  commit.

Design:

- CS_PbfAnisotropy already has each particle's neighbour count (`neighbours`) and the
  weighted mean offset of its neighbours (`mean`).
- A particle is **buried** when its neighbourhood is full and balanced:
  `neighbours >= BuriedNeighbours` and `length(mean) <= BuriedOffset * h`, where `h` is the
  kernel's neighbour radius.
  - A buried particle sits about two spacings or more inside the fluid, so its neighbours'
    ellipsoids cover it from every direction.
- Mark it without a new buffer, for example by storing `-neighbours` in
  `Anisotropy[i * 3 + 2].z`.
  - First grep every reader of that component in all `.hlsl` files.
  - If any reader uses it, choose another encoding and say which.
- CS_FluidSplat: the ellipsoid depth loop and the surfel pass skip buried particles before the
  rectangle loop. The thickness/whitewater loop still includes them.
- Start values:
  - `BuriedNeighbours`: the neighbour count measured for interior particles at rest;
  - `BuriedOffset`: 0.1.
  - Measure and report the buried fraction for the lake start and for the water start at f45
    and f150 (a temporary counter or debug view is fine; remove it before committing).
- Validation is about holes. Compare the fluid depth dumps before and after, at 4K, for the
  default and close views, water start f45 and f150, lake start f300. Report:
  - pixels that had water before and none after (must be 0, or explain each);
  - mean and max |delta z| over pixels with water in both;
  - the HDR_Final differences, as in WO-P1.
- Perf target: Fluid Splat close/water at least 1.0 ms below the WO-P1 result.

## WO-P3: shade: per-frame sky term and Fresnel-gated SSR

Owns: `CS_ParticleShade.hlsl/.glsl`, after WO-P1 is committed.

1. `SkyIrradiance()` gives the same value for every pixel.
   - Thread 0 of each 8x8 group computes it into `groupshared float3`, followed by
     `GroupMemoryBarrierWithGroupSync()`.
   - Every thread must reach the barrier: move the `Mode == 0` and bounds returns below it.
     Out-of-range threads pass the barrier, then return.
   - Measured worth: 0.17 ms at 4K close-up.
2. **Trace SSR only where it can be seen.**
   - The Fresnel term does not depend on the trace: compute it before `TraceReflection`.
   - `ssrWeight = smoothstep(F0, F1, fresnel)`. When `ssrWeight == 0`, skip the trace and use
     the environment only. Otherwise use
     `reflected = lerp(env, hitColor, hitWeight * ssrWeight)`.
   - Choose F0 and F1 so that the 4K default and close views change by less than 0.5 level
     mean absolute difference.
   - Report the shade time and the image differences for at least three (F0, F1) pairs.
3. Report Fluid Surface Shade GPU time in the four cells, before and after.

## WO-P4: tiled deferred lights: CPU work and uploads proportional to the lights in use

Owns: `T850/Framework/src/scene/RenderQuad.cpp` and `T850/Framework/include/scene/RenderQuad.h`.
Do not touch `FS_Quad.hlsl/.glsl`; keep the texel layout the shader reads.

- Today at 4K:
  - 8,160 tiles of 32 px, `kMaxTiledLightsPerTile = 128`, RGBA32F per index entry;
  - `m_tiledLightIndexData` is 16.7 MB and is filled with zeros every frame (about 1.2 ms of
    CPU);
  - it is uploaded whole, through a new 16.7 MB staging buffer, whenever any (tile, light)
    assignment changes, which is every frame the camera moves.
- The shader reads only the first `count` entries of a tile. Therefore:
  1. Clear only the headers and counts, not the index array.
  2. Size the index texture's width to the largest per-tile count in use, rounded up to 8 and
     capped at 128. Recreate the texture only when it must grow.
  3. Upload only that width.
- Validation:
  - DayScene scene 1 (`--scene 1`) and Backyard 8 HDR_Final dumps before and after: byte-identical
    is expected.
  - CPU `render.pass.Deferred Light Volumes` at 4K before and after, from
    `backyard8_perf.py --realtime` (WO-P0) or the same telemetry by hand.
  - Texture upload bytes per frame from telemetry, before and after, in a run where the camera
    moves. Scene 1 follows a spline camera in normal mode; verify it moves.
- This file serves every scene and every API. Add no API-specific code.

## WO-P5: documentation

Owns: `documentation/scenes/backyard8-perf4k.md` (new), one row in `documentation/README.md`,
and the cost section of `documentation/scenes/backyard8-screen-space-fluid.md`.

Starts after Claude's final measurements are in "Notes from Claude". Content:

- the measurements;
- what changed in each work order;
- the new setting;
- how to run `backyard8_perf.py`;
- the GPU-sharing caveat;
- the remaining costs: shadow accumulation, the solver step, and the candidates below.

## Later candidates (not scheduled)

- Shadow Accumulation (0.7-1.0 ms at 4K) runs full-resolution PCF with manual point taps plus
  SSAO in `FS_Quad.hlsl`, shared by every scene. Possible changes: half-resolution SSAO, and
  hardware comparison samplers for PCF. These are engine-wide and need Windows validation.
- SSR on a hierarchical (min-depth) pyramid instead of an 80-step linear march.
- A profiler scope for the PBF solver. The profiler frame starts in the draw phase
  (`Application.cpp:900`), after the update that records the solver, so a scope in the step
  is outside it today.

## Status

| WO | Owner | State | Commits | Notes |
|---|---|---|---|---|
| P0 | qwen-5090 | done | dd412a8 | reviewed: owned files only; tooling works; 4K numbers UNGUARDED (GPU at 100% from qwen-local) |
| P1 | qwen-local | in progress | | dispatched 2026-11-11 |
| P2 | qwen-5090 | P2a in progress | | P2a (CS_PbfAnisotropy only) dispatched; P2b (splat) blocked by P1 |
| P3 | | blocked by P1 | | |
| P4 | qwen-5090 | done | 4389a36, 6961c93 | Claude accepted 6961c93 (code); Claude measures the CPU numbers. 4389a36's body misdescribes the Deferred diff (point lights restored in static views) |
| P5 | | blocked by P0-P4 and Claude's final numbers | | |

## Notes from Claude

### Timing while qwen-local works (applies to every work order)

- qwen-local generates on this GPU almost all the time it works, so a guarded timing run rarely
  finds the GPU idle.
- `backyard8_perf.py` gets `--no-guard` for functional checks. Its output is labelled
  UNGUARDED and is not valid for comparison.
- The guard also counts any other DayScene process (another worker's run) as contention.
- Acceptance timing:
  - qwen-local measures its own changes in the foreground. It does not generate while it waits
    for its tool, so the run is clean once no other DayScene runs.
  - qwen-5090 reports UNGUARDED numbers unless the GPU is idle.
  - Claude re-measures every accepted change with a guarded run.
- Never run DayScene or timing in the background while editing. Shaders compile at runtime,
  so a run that starts mid-edit compiles a half-edited kernel.

### WO-P0 reviewed (`dd412a8`): accepted

- The camera override is proven by the byte-identical camera-dependent targets (GBuffer,
  Deferred, WaterDepth).
- The HDR_Final and FluidParticles differences between two identical runs are the known solver
  nondeterminism. That is why the gate compares statistics, not bytes. It is pre-existing; no
  action in this work.
- The UNGUARDED 4K numbers show contention (39 fps real time). Claude re-measures guarded when
  the workers are idle.
- **For WO-P4:**
  - expect byte-identical HDR_Final for DayScene scene 1 (no fluid);
  - for Backyard 8, compare only the non-fluid targets (GBuffer_*, Deferred) byte for byte,
    and HDR_Final with the gate.
- `T850/Assets/Scenes/scratch/` (WO-P1's scratch configs) must be deleted before WO-P1
  finishes. Never commit it.

### Acceptance correction: visual equivalence against run-to-run noise (WO-P1 to WO-P3)

- Two identical runs already differ in HDR_Final (solver nondeterminism), so "matches the
  pre-change dumps" means: within about 1.5 times the noise floor.
- The noise floor is the difference between two runs of the before build with the same command.
  Report both numbers.
- Comparisons inside one build (for example `thicknessDownsample` 2 against 1) are not
  affected.
- Uncommitted work of another work order in the shared tree (WO-P4's `RenderQuad.cpp`) is part
  of every after build. Give the before worktree the same files, or compare after that work
  order is committed.

### Worktree runtimes (every before/after comparison)

A worktree's `T850/bin/SteamDeck/Release` links:

- Shaders, Scenes, Layouts and Fonts to the worktree's own `T850/Assets/<dir>`;
- only Models and Textures to the main tree (downloaded, not in git).

Shaders compile at runtime from the linked directory. A worktree that links the main tree's
Shaders runs another work order's uncommitted kernels.

### WO-P4 reviewed (`4389a36`): needs a follow-up commit

What is right:

- `ensureTiledLightTextures` no longer wipes the frame's data, and any recreation forces an
  upload.
- The index entries are written after `ensure`, with the texture's width as the stride.
- Byte-identical scene 1 HDR_Final; self-test 78 PASS; validators exit 0.

The commit body misreads the Backyard 8 Deferred difference. It is not noise:

- Two runs of the before build give byte-identical Deferred output, and HDR_Final too for the
  lake start.
- Before against after, every pixel that changes by more than 4 levels gets **brighter**
  (4,219 pixels, 0 darker), centred on the patio around the point light at (0, 1.6, 4.6).
- This is the pre-existing upload-wipe bug being fixed. Until now, static views (every dump,
  gate run and capture) rendered without the five point lights, which appeared only once the
  camera moved.
- The gate still passes. Claude re-freezes the references later, after visual sign-off.

To fix in a follow-up commit (`WO-P4: ...`):

1. **Resize.** When `tilesX` or `tilesY` change, the header is recreated but the index texture
   keeps its old height (`tileCount` rows), while uploads use the new tile count. A larger
   window then copies past the texture's last row. Recreate the index texture whenever the
   tile count changes, as well as when its width must grow. Test a resize: two runs at
   different `--width/--height`, and if possible a resize mid-run; report any validation
   errors.
2. **Missing acceptance numbers**, both at 3840x2160:
   - CPU `render.pass.Deferred Light Volumes` before and after (`backyard8_perf.py --realtime`;
     `--no-guard` is fine for a CPU number);
   - texture upload bytes per frame while the camera moves (scene 1's spline camera, telemetry
     `uploads`).
3. Correct the description of the Deferred change in the follow-up commit body: point lights
   restored in static views, after brighter only.

### WO-P4 follow-up reviewed (`6961c93`): code accepted

- `ensure` runs before the header and index data are written.
- Tile-count changes recreate the index texture.
- The texture's width is the stride, and any recreation forces an upload.
- The shader uses `LightCameraInfo.w` only as a per-tile clamp, so a variable width is safe.
- The CPU "before 0.26 ms" was measured on `4389a36`, which had already dropped the 16.7 MB
  fill, so it is not the original code.
- Scene 1's spline run uploaded 0 bytes in both builds, so it does not exercise this path.
- Claude measures the true before (`dd412a8`) against the after when the GPU is idle.

## Requests to Claude

- WO-P0 (qwen-5090, commit dd412a8): the scene-camera equality check
  (`--backyard-camera 0 2.4 -13.5 0.22 0` vs no flag, `--dump-frame 150`)
  shows GBuffer_Albedo, Deferred, WaterDepth and GBuffer_Normals are
  byte-identical, but HDR_Final and FluidParticles differ between two
  runs of the same command with no flag at all. The cause is the PBF
  seed relaxation: `RelaxSeed` in `PbfFluid.cpp` uses
  `g_threadPool->ParallelFor` with a non-deterministic reduction order,
  so the relaxed sphere positions differ by ~1e-4 m between runs, and
  the splat/shade amplifies that into visible pixel differences.
  This is pre-existing, not introduced by WO-P0. The camera override
  itself is correct: the camera-dependent RTs are byte-identical when
  the flag equals the authored camera. If the gate or a future work
  order needs byte-identical dumps, the seed relaxation needs a
  deterministic single-threaded path or a fixed seed.

