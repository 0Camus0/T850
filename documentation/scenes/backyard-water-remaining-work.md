# Backyard Water — Remaining Work and Implementation Directions

This document lists every known open issue in the backyard pool water (scene 7) after commit
`e90752b4` ("Fix backyard water rendering and rebuild the solver on a staggered grid"), with the
root cause, the fix to implement, the files involved and how to prove the fix works. Section 0 has
the current status (as of code commit `47500166`).

It is written so that any implementer (GLM, DeepSeek or a human) can pick **one task**, follow it
in isolation, and hand back a change that can be reviewed against the acceptance criteria. Tasks
are ordered by priority. Each task states which other tasks it depends on.

The original implementation record is
[backyard-water-simulation.md](backyard-water-simulation.md). Its sections on the solver
(6–8, 14–16) describe the **old collocated solver** and are superseded by section 2 below until
task W-16 updates them. The order in which the tasks are executed is in
[backyard-water-staged-plan.md](backyard-water-staged-plan.md).

---

## 0. Progress and review status

| Task | Status | Commit | Review (independent re-run) |
|---|---|---|---|
| W-18 Vulkan descriptor-pool exhaustion | Done | `a18d34e5` | Accepted. Recycles only compute-opened frames past 75 % of the pool |
| W-12 Self-test gaps | Done | `853fff34`, `08d441fa` | Accepted. `--backyard-selftest` PASS on D3D12 and Vulkan; gates are calibrated against measurements |
| W-01 Water invisible on GL/WebGPU | Done | `ea80665b` | Accepted. GL and WebGPU scene 7: 0 errors |
| W-08 Domain ceiling is a lid | Done, **corrected in review** | `ea80665b` + review commit | Gradient/pressure ghost-pressure mismatch at the ceiling fixed; see W-08 |
| W-13 Dead code | Done | `ea80665b` | Accepted |
| W-19 Ball sits in a funnel | Done (review) | `fa5422ff`, `33baad50` | Surface under/around the floating ball flat and stable; gates 7 and 8 |
| W-20 Scene index ignored with `--api` | Resolved: by design, documented | — | `config.json` is only read with `--config`; always pass `--scene 7` |
| Tooling: fluid debug view | Done (review) | `4d21e836`, `93dff3d9`, `5d640c2e` | Wireframe, voxel and whole-volume velocity overlays in the ImGui panel; D3D12 upload-ring crash fixed (per-layer caps); bulk-water α statistic; see 4.5 |
| W-11 item 3: grid follows the cell size | Done (review) | `d6b65147` | Width, depth and height derive from one cubic cell size; GUI sliders linked |
| W-11 items 1, 2, 4: JSON tunables, three solver values, comment | **Done** (Stage 4) | `5511e7e9` + `e974f4e5` | Accepted. `Scenes/BackyardWater.json` drives 101 settable keys; strict key validation; `alphaCompression`, `maxSubstepDt` and the volume-check interval are tunables with GUI sliders. Review added the missing Android registration of `BackyardWaterJson.cpp` |
| W-15 Solver and frame cost | **Done where it pays** (P1, P4, P5, P6, P7, P8) | `3a371903`, `c911381a`, `11357fab`, `efdac3d8` | 720p, 0 balls: D3D12 222 → **298.6** fps, Vulkan → **280.6**; 30 balls 268.6 / 259.8; 0 spikes. P2 and P3 were attempted and rejected on measurement, P9 is debug-only. See the 2026-10-31 pass in W-15 |
| W-02 R32F storage for α/pressure/divergence | **Done** | `14c705f4` + part 2 | α, pressure, divergence are R32F; still-pool loss 0.693 % → 0.001 %. The gate shift was an off-centre isotropy probe, an fp16-calibrated gate 5 floor and trapped air read as missing water. Tiled surface kernel: 720p D3D12 306.8 → 415.3 fps, Vulkan 353.4 → 402.3 |
| W-03 Mass lost every substep | **Mostly done** (attempt 3) | `b5ae57fa`, van Leer flux | Transport loss is ~0 (deficit is the displaced body volume); van Leer/minmod face value lifts bulk 0.9969 → 0.9987 and cuts cells below 0.98 by 79 % for free. Splitting and SLIC measured and rejected. Three acceptance lines marginal — see W-03 |
| W-04 GPU→CPU readbacks | **Done** | `c911381a`, `ad7e019e`, `4e93d9d5` | Correction no longer reads back (GPU reduce + GPU deficit); probes only at info logging. Step 5 added the async readback pair on D3D12/Vulkan (`Request/ConsumeComputeReadback`); step 3 made the diagnostics buffer persistent with a kernel-side clear. Under 1 % on every perf row |
| W-06 Ball does not feel the water | **Done** | `4e93d9d5`, this session | Buoyancy floats on the simulated surface averaged over an annulus; step 3 added the annulus current so drag acts on (v − flow). Step 4 measured: heave damping 0.15 stays (0.05 → 128 mm residual bob, 0 → 307 mm) |
| W-07 Water under the ball is deleted | **Attempts 1 and 2 measured, not shipped** | — | Attempt 1 failed gate 8 (0.034 m flicker). Attempt 2 established *why*: any pass that respects the binary wall mask moves a whole cell when the footprint flips, so the fix needs a per-cell solid fraction (sub-cell coverage) in the mask and the transport, not a smarter displacement pass. Design recorded in W-07 |
| W-05 Moving body re-uploads the wall mask | Done differently (review) | `c911381a` | Mask restamped on the GPU in a persistent buffer; no upload after startup. The analytic-body design is no longer needed for cost |
| W-09 Water optics: depth, refraction, Fresnel | Done | `517ae6bf` | Accepted. Floor texture logs depth 0.300…1.560 m; `MaxPrimitiveTextures` 24 → 28 is safe (bones/lightmap bind outside the array) |
| W-10 Water invisible to depth post effects | Done, **redesigned in review** | `96d84638` + `47500166` | The extra pass duplicated `Deferred:DEPTH`; removed. CoC now uses the nearer depth; see W-10 |
| W-21 Vulkan fluid self-test fails inside scene 7 | **Done** (Stage 3c) | `5798477c` | Accepted. Root cause was uninitialised volume memory, not Vulkan frame state; `TryInitialize` now clears; 6/6 PASS scene 7 Vulkan |
| W-22 Spawn more balls (random place, size, weight) | **Done**, uncapped in review | `0a981b8b` + `9f6e7a2f` + `97009aef` | Any number of balls (100 tested on D3D12/Vulkan/D3D11); water Z-mirror fixed; see W-22 |
| W-23 Textured materials with normal maps | **Done** (Stage 4b) | `507ef1a4` | CC0 ambientCG textures with NormalGL, world-box UVs |
| W-24 House roof is upside down | **Done** (Stage 4a) | `2114fa0e` | Sign flipped, gables closed |
| W-25 A different PBR material on every ball | **Done** (reviewer) | `10edb63d` + `e055efec` + `ff66c039` | 32 CC0 sets in R2; cube-sphere ball with UVs; per-instance maps without a new permutation; random shuffle bag. Gate 10 (headless) and the frame probe landed in `ff66c039` |
| W-26 The surface kernel compensates for trapped air | **Measured; cannot be removed yet** | this session | Removing it costs 4.8 mm more moat: `covered min−ring` −15.9 → **−20.7 mm** (D3D12, limit 20) and −13.8 → −19.1 (Vulkan). The term is still hiding water W-07 deletes, so it stays until W-07 lands; the measurement is recorded in W-26 and in the implementation record §7 |
| W-27 The wind tunable does nothing | **Open** (found this session) | — | Wind is applied only above `SurfaceLevel · Height`, which with `surfaceLevel` 0.96 and fill 0.788 lands in the air rows. 8× wind left a scene-7 run bit-identical. Not fixed here because it is not on the water path this session touched |
| All others | Open | — | — |

Review of `9693f79d..ea80665b` (2026-10-28): x64 Release builds; `--fluid-selftest` and
`--backyard-selftest` PASS on D3D12 and Vulkan; scene 7 on OpenGL and WebGPU logs 0 errors. Registry
and shader `FluidConstants` sizes agree in all 16 kernels (20 DWORDs). Build registration validator
PASS; `BackyardSelfTest.cpp` is correctly absent from `AndroidBuild.cmake` because Android does not
compile `App.cpp`. Minor nits left for the next stage that touches those files: the comment in
`VulkanDriver.h` refers to `RecycleOffscreenComputeFrame` (the function is
`RecycleOverflowingComputeFrame`); the still-pool gate comment says "1 mm" while the code checks
5 mm.

Review follow-up changes (by the reviewer, not the stage implementer), all on `backyard_scene`:
`d2291f1a` (open-ceiling ghost pressure in the gradient kernel, see W-08), `fa5422ff` and
`33baad50` (W-19 ball funnel and its flicker follow-up), `4d21e836`, `93dff3d9` and `5d640c2e`
(fluid debug view, its D3D12 crash fix and the whole-volume velocity layer, see 4.5), `d6b65147`
(grid follows the cell size, W-11 item 3) and `3a371903` (W-15 fixes). Pull them before starting
Stage 3.

Review of Stage 3 (`517ae6bf`, `96d84638`, 2026-10-29): W-09 accepted as delivered. W-10's depth
target was correct but redundant — the forward pass already writes the water into `Deferred:DEPTH`,
identical pixel for pixel — and had no consumer. The reviewer removed the pass and made CoC consume
the nearer depth (`47500166`, details in W-10). Re-run: `--backyard-selftest` PASS on D3D12 and
Vulkan; scenes 7, 1 and 6 log 0 errors on D3D12, Vulkan, D3D11, GL and WebGPU. Found in passing:
W-21. Step-by-step brief: section 0 of [backyard-water-staged-plan.md](backyard-water-staged-plan.md).

Review of Stage 3c and Stage 4 (`5798477c`, `5511e7e9`, 2026-10-29): both accepted. W-21's diagnosis
(intermittent, two discrete outcomes → uninitialised memory) and its fix in the solver rather than
the harness are right. Stage 4's separate `Scenes/BackyardWater.json` is an accepted deviation. One
defect fixed by the reviewer (`e974f4e5`): `BackyardWaterJson.cpp` was not in
`cmake/AndroidBuild.cmake`, so the Android link would fail; `ValidateBuildRegistration.ps1` does not
check Backyard files, which is why it still said PASS. A reported Vulkan slowdown is **not** from
these stages: the same 172–177 fps reproduces at `7bd0afe9`, `945b2f7c` and `3a371903` (W-15).
**Next: Stage 4a (W-24 roof), 4b (W-23 textures), 4c (W-22 more balls), then Stage 5a (W-02).**

---

## 1. How to use this document

1. Pick one task. Do not combine tasks unless the task says so.
2. Read section 2 (current architecture) and section 3 (rules) first. Most failures in previous
   rounds came from breaking a rule in section 3, not from the physics.
3. Implement only what the task asks. No drive-by refactors, no reformatting.
4. Run every command in the task's **Acceptance** list and report the measured numbers, not just
   "pass".
5. Hand back: the diff (or complete files), the measured numbers, and anything that did not work.

### Suggested model per task

The "Suggested" field is a recommendation, not a rule. Observed behaviour so far:

| Model | Strengths | Watch for |
|---|---|---|
| DeepSeek (DS4) | Fast (≈70 s for four complete kernels), follows output formats exactly, good at self-contained HLSL | Subtle index bugs: wrote `cell - offset` where `offset` was already negative; produced a `CellIndex` with a term multiplied by `0u`; used an `[unroll]` loop over a local array (rule R5) |
| GLM | Large context (500k), careful with multi-file C++ | Very slow with reasoning on (stalled > 20 min twice); run with reasoning disabled (`chat_template_kwargs: {enable_thinking: false}`) |

Every delivered kernel was correct in structure and wrong in one detail at least once. **Review
every line of index arithmetic.**

Helper used for previous rounds (outside the repo): `D:\Code\QwenFlashT850\backyard-diag\llm.py`
(OpenAI-compatible client, optional `nothink` argument) and `extract.py` (pulls
`=== FILE: name ===` blocks out of a response and diffs them against the sources).

---

## 2. Current architecture (as of e90752b4)

### 2.1 Per-substep pipeline

`FluidSimulation::Step` splits the frame into substeps (`maxSubstepDt` = 1/120 s, and enough to
keep the ball under half a cell per substep; capped at 12). Each substep (`StepInternal`):

| # | Kernel | Purpose |
|---|---|---|
| 0 | CPU `AdvanceSphere`, `UpdateWallMaskForBody` | Integrate the ball; stamp its cells into the wall mask (CPU, re-uploads the mask) |
| 1 | `CS_FluidAdvect3D` | Semi-Lagrangian advection of the staggered velocity |
| 2 | `CS_FluidForces3D` | Viscosity (`ν·dt/h²`, capped at 1/6), gravity on V faces, optional wind, damping |
| 3 | `CS_FluidObstacleVelocity3D` | **Boundary kernel**: every solid face gets its boundary value (0, or the ball's velocity) |
| 4 | `CS_FluidDivergence3D` | Divergence of liquid cells |
| 5 | `CS_FluidPressure3D` × 2·N | Red-black SOR, ghost-fluid free surface |
| 6 | `CS_FluidGradient3D` | `u -= dt·∇p` on liquid faces; solid faces get boundary values |
| 7 | `CS_FluidExtrapolate3D` | One layer of velocity extrapolation into air faces |
| 8 | `CS_FluidAlphaAdvect3D` | Flux-form upwind volume fraction + bounded interface sharpening |
| 9 | `CS_FluidSurface3D` + `CS_FluidSurfaceCopy` | 2D surface texture for the water mesh |

Once per frame (every 15 frames) `CorrectVolumeFraction` reads the whole volume back to the CPU
and adds the missing water to liquid cells that still have room.

### 2.2 Grid conventions (MAC)

* Cells `(i,j,k)`, `0 ≤ i < W`, `0 ≤ j < H`, `0 ≤ k < D`, size `CellSize` (0.09 m). +Y is up.
* Velocity texture texel `(i,j,k)` stores the faces on the **positive** side of cell `(i,j,k)`:
  `.x` = u at `(i+1, j+½, k+½)`, `.y` = v at `(i+½, j+1, k+½)`, `.z` = w at `(i+½, j+½, k+1)`,
  `.w` = 0. The −X face of cell `A` is texel `A−e_x`; at index −1 it is the domain wall (0).
* Out-of-range cells are **solid** in every kernel.
* Cell classes: SOLID (mask ≠ 0 or out of range), LIQUID (α ≥ 0.5), AIR (α < 0.5).
  `T8_LIQUID_THRESHOLD` (HLSL) and `FluidSettings::airThreshold` (C++) must both be 0.5.
* Face classes: SOLID (either side solid), LIQUID (not solid, either side liquid), AIR.
* Pressure: `∇²p = ∇·u / dt` (`PressureScale = h²/dt`), then `u -= dt·∇p`. Pressure is stored in
  physical units so the previous substep's field stays a valid warm start when dt changes.
* Ghost fluid: across a liquid→air face, `θ = clamp((αL−0.5)/(αL−αA), 0.1, 1)`,
  `p_ghost = p_L·(1−1/θ)`.
* Body cell test (kernels with `ObjectConstants`): centre
  `(WorldOriginX + i·h, BasinMinY + (j+½)·h, WorldOriginZ + k·h)` inside the sphere.
  `WorldOriginX/Z` already include the half-cell offset.

### 2.3 Surface texture contract (`CS_FluidSurface3D`, slot t23)

R = elevation delta from the authored water plane (m); G = foam; B/A = tent-filtered height
differences per texel (the fragment shader multiplies by `2/cellSize`). Column height counts
solid cells that lie below wet water. Dispatched as `(width, depth, 1)`: the grid row is `id.y`.

### 2.4 Baseline measurements to keep or beat

D3D12, x64 Release, RTX 4080 Laptop, scene 7, default tunables (ball r 0.25 m, density 0.6,
dropped from 1.0 m). Measured from the `[Backyard]` log lines (see 4.3).

| Metric | Value at e90752b4 |
|---|---|
| Still pool surface rms (before impact) | 0.1–0.3 mm |
| Still pool mean level (no ball, 2 s) | **sinks ≈ 1.1 cm/s** (−1.5 mm at 0.2 s, −10.5 mm at 2 s); found in Stage 0, missed by the original rms-only baseline |
| Mean speed before impact | ≈ 0.006 m/s |
| Mean speed at t ≈ 10.7 s | 0.0035 m/s (decaying) |
| Surface rms at t ≈ 11 s | ≈ 2.2 cm at e90752b4, dominated by the −35 cm hollow under the floating ball. **After W-19: rendered surface min −2.1 cm, rms 1.1 cm at t = 10.7 s** |
| Water over the steps | ≤ +5 cm |
| Ball at rest | submerged fraction 0.60 (= density ratio) |
| Volume correction deficit | ≈ 300 cells (0.36 %) every 15 frames with the ball; **not constant** at rest without the ball (202 → 749 cells over 2 s, Stage 0) |
| Alpha volume drift (with correction) | −0.03 % |
| Fluid self-test | PASS D3D12 and Vulkan; projection ratio 0.448 at 50 Gauss–Seidel sweeps (0.078 with SOR) |
| Other APIs | D3D11 full solver, 0 errors; OpenGL/WebGPU: see W-01 |

---

## 3. Rules every change must follow

These are not style preferences. Each one has caused a silent failure, a GPU hang or a wrong
result in this code base.

* **R1 — HLSL 2018 does not short-circuit `||`, `&&` or `?:`.** The engine's DXC flow compiles
  HLSL 2018. `InRange(c) && WallMask[Index(c)]` reads the buffer even when `c` is out of range.
  On D3D12 the structured buffer is a root SRV with no bounds checking and the read faulted the
  GPU (`DXGI_ERROR_DEVICE_HUNG`); Vulkan happened to survive. Always guard with an early `return`.
* **R2 — Registry and shader must agree exactly.** Every kernel is declared in
  [compute_kernels.json](../../T850/Assets/Shaders/compute_kernels.json). `binding_index` must equal
  the `[[vk::binding(N, 0)]]` value; `constant_count` must equal the reflected DWORD count (a
  cbuffer is padded to 4 DWORDs). A mismatch makes the driver reject the pipeline **with no
  error**. Every declared resource must be used, or the compiler strips it and the layout no
  longer matches.
* **R3 — Storage textures are write-only.** Keep `[[spv::nonreadable]]` and the
  `[[spv::format_…]]` attribute under `#ifdef T850_SPIRV`. All fields are ping-ponged.
* **R4 — Only RGBA8 and RGBA16F storage formats exist** today (see W-02).
* **R5 — No loops over local arrays.** An `[unroll]` loop over a local `const` array collapsed
  onto element 0 on this engine. Write six neighbour calls out explicitly.
* **R6 — Staged self-test path.** `FluidSimulation::StepStage` re-implements single stages for
  the self-test. Any binding change to divergence, pressure, gradient, surface or the boundary
  kernel must be mirrored there.
* **R7 — Binding order on the host.** `DispatchVolume` appends bindings in a fixed order
  (b0, t0, t1, u0, t8, t2, b1). Kernels that do not declare a resource must not receive it.
* **R8 — Comments.** One short line, only where the code cannot speak for itself.
* **R9 — Do not commit `T850/config.json`** (rewritten by the app at runtime) or generated
  `.glb` files (git-ignored; regenerate with the Blender script).

---

## 4. Build and verification

### 4.1 Build (x64 Release)

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  D:\Code\QwenFlashT850\T850-backyard\T850\T850.sln '/t:Framework;DayScene' `
  /p:Configuration=Release /p:Platform=x64 /m:2 /nologo /v:m
```

If the build fails with `C2011 'std::align_val_t'` or `__builtin_assume_aligned` redefinitions,
delete the stale precompiled header
`T850\Lib\VStuff\Framework\Release\x64\Framework.pch` and rebuild. Use `/m:2`; higher parallelism
has crashed the compiler on DayScene.

Shader-only changes do not need a rebuild: kernels are compiled at startup from
`T850\Assets\Shaders`.

### 4.2 Fluid self-test (must pass on D3D12 and Vulkan)

```powershell
cd D:\Code\QwenFlashT850\T850-backyard\T850\bin\x64\Release
.\DayScene.exe --scene 7 --api d3d12  --fluid-selftest --logLevel info --logFile selftest_d3d12.log
.\DayScene.exe --scene 7 --api vulkan --fluid-selftest --logLevel info --logFile selftest_vk.log
```

Look for `[FluidSelfTest] PASS`. Report the `projection` and `massConservation` lines.

`--fluid-selftest` checks the solver in isolation on its own 32×16×16 grid. Since W-21 it passes
under any scene, so use `--scene 7` like every other command.

The pool-level gates (still pool, isotropy, mass, moving boundary, surface texture readback, open
ceiling, floating body, floating-body flicker) run the real Backyard solver headless:

```powershell
.\DayScene.exe --api d3d12  --backyard-selftest --logLevel info --logFile backyard_d3d12.log
.\DayScene.exe --api vulkan --backyard-selftest --logLevel info --logFile backyard_vk.log
```

Look for `[BackyardSelfTest] PASS`. `--backyard-no-ball` skips the ball-dependent gates;
`--backyard-selftest-frames N` changes the run length; `--backyard-cell-size <m>` runs on the grid
that cell size implies (the `[Fluid] Grid WxHxD @ h` line reports it). The gates are calibrated on
the default 0.09 m grid: at other sizes the isotropy gate (fixed radius and time) is informational.

### 4.2.1 Images

Frame dumps are PPM. Convert crops with a validating converter (each PNG row needs its leading
filter byte; a PNG without it is rejected by image viewers and by the chat tooling). The one used for
reviews is `D:\Code\QwenFlashT850\backyard-diag\ppm2png.py <in.ppm> <out.png> [x0 y0 x1 y1] [scale]`;
it refuses to write a malformed or oversized file.

### 4.3 Scene run with measurements

```powershell
.\DayScene.exe --api d3d12 --scene 7 --width 1280 --height 720 --dump-frame 3000 `
  --logLevel info --logFile scene.log
```

The run exits at frame 3000 (≈ 11 s of simulation at ~270 fps). Every 60 frames it logs:

* `[Backyard][fluid] … meanSpeed maxSpeed nonFinite`
* `[Backyard][sphere] y vy submerged t`
* `[Backyard][surface] delta vs water plane (m): min max mean rms` — read from the rendered
  surface texture (`FluidSimulation::ReadSurface`) since W-19; before that it was re-derived from
  alpha with the static mask and could not see the ball
* `[Backyard][columns] surface delta (cm) along X` — through the ball's row, same texture
* `[Backyard][timing] … alpha volume`

With `--logLevel debug` the log also contains `[Fluid] VolumeCorrection … deficit=` and the
per-frame `substeps=` lines. Zero `[ERROR]` lines are required.

### 4.4 Visual check

`--dumpSnapshot-seconds 0.9` (and `2.0`) writes `dumps_d3d12_f*/RT_Dump_BackBuffer.ppm`. After an
impact, rings must spread in **all** directions from the ball and reflect from the walls.

### 4.5 Debug view (ImGui "Backyard Water" → "Debug view")

`FluidDebugRenderer` (`Framework/src/simulation/FluidDebugRenderer.cpp`) draws line overlays read
back from the solver every frame a layer is on (a GPU stall; expect ~70–100 fps instead of ~280).
Nothing is created or read back while every option is off.

| Option (as labelled in the panel) | What you see | Colour |
|---|---|---|
| water mesh wireframe | The actual render mesh (288×144 vertices), displaced on the CPU exactly as `VS_Mesh` does | white, translucent |
| solver surface, 1 vertex per column | One vertex per surface texel (100×50): the resolution the simulation really has | yellow |
| liquid voxels at the surface, alpha >= 0.5 | Cells at least half full with air above: the blocky surface the pressure solve sees | blue |
| partially filled cells, height = alpha | Interface cells only (0.02 < α < 0.98 with an air neighbour), each box filled to its water fraction | teal |
| solid voxels: steps grey, body orange | The wall-mask shell touching fluid: steps, and the ball as the solver voxelises it | grey / orange |
| velocity on a vertical slice | Cell-centred flow arrows on one XY slice; follows the ball by default. Sliders: line length (m per m/s) and slice Z row | magenta |
| velocity in the whole water volume | An arrow (shaft and head) per liquid cell over the full 3D grid, coloured by speed. Sliders: one arrow every N cells (0 = auto, sized to the 15 000-arrow cap; N = 2 at the default grid), hide cells slower than (m/s, default 0.005, above the still-pool noise), and the shared line length | cyan < 5 cm/s, yellow < 25 cm/s, red faster |
| solver grid bounds | The simulation box | green |
| x-ray (ignore scene depth) | Draws every layer through walls and the ball | — |
| hide shaded water | Hides the rendered water so only the overlays remain | — |

Below the options the panel shows the grid size, cell size, per-layer counts as drawn/total, and a
**bulk water** line: mean and minimum α of liquid cells with no air neighbour, and how many are
below 0.98 (they should all be 1; see W-03). With the volume field on it also shows how many liquid
cells move faster than the threshold and the fastest liquid cell (speed, cell index, α).

The box layers are deliberately surface-only: liquid voxels are the top water cell of each
column, partially filled cells the water/air interface. Drawing every one of the ≈ 86k water cells
as a box would be an opaque mass of lines and would exceed the D3D12 upload ring below. Use the
whole-volume velocity field (with "hide shaded water" and "x-ray") to look inside the pool.
Measured at rest (t ≈ 25 s, default grid): ≈ 56k of 86k liquid cells still move faster than
5 mm/s, the fastest ≈ 0.018 m/s next to a wall; right after the impact the fastest is ≈ 0.8 m/s.

Each voxel layer draws at most 8000 boxes (`kMaxBoxesPerLayer`) and the volume field at most
15 000 arrows (`kMaxVolumeArrows`); above that they draw every Nth candidate, so the coverage stays
spread over the pool. The caps exist because D3D12 streams
dynamic vertex buffers through a 16 MB per-frame ring shared with the constant buffers: the first
version drew one box per diluted water cell (≈ 81k boxes, 15.6 MB) and overflowed it on every
frame (`Ring buffer overflow`, then a crash). Vulkan has no such ring, which is why it only
failed on D3D12.

Depth testing uses GBuffer depth, which excludes the water, so voxels under the surface stay
visible. Use this when changing grid size or cell size: it shows how coarse the solver is compared
with the render mesh, and how the ball is voxelised. Verified on D3D12, Vulkan and D3D11 with 0
errors; GL/WebGPU run no solver, so nothing is drawn there. The renderer consumes
`ReadSurface`, `ReadVolumeFraction`, `ReadVelocity` and `ReadWallMask`; a task that changes their
output layout (W-02 storage, W-03 transport) must update the renderer in the same change.

---

## 5. Tasks

Priority: **P1** visible defect or correctness bug, **P2** quality/robustness, **P3** cleanup.
Size: S (< 100 lines), M (100–400 lines), L (> 400 lines or several subsystems).

### W-01 — Water is invisible on OpenGL and WebGPU (P1, S, suggested: either)

**Problem.** On `--api gl` and `--api webgpu` the solver is unavailable
(`SupportsComputeTextures3D()` is false), the scene logs "water renders without displacement",
and then logs `GetShader miss … key 0x000008000810000F (pass=1)` **every frame** (400 errors in a
400-frame run). The water mesh is never drawn.

**Root cause.** [BackyardScene.cpp](../../T850/DayScene/BackyardScene.cpp) sets
`instance.PersistentFeatureBits = ShaderKey::FLUID_SURFACE` unconditionally when the water mesh is
loaded. That permutation is only compiled by `EnsureWaterShaderPermutations`, which runs after the
solver initialised successfully, and the GLSL/WGSL mesh shaders (`VS_Mesh.glsl/.wgsl`,
`FS_Mesh.glsl/.wgsl`) do not implement `FLUID_SURFACE` at all.

**Fix.** Set `FLUID_SURFACE` on the water instance only after `TryInitialize` succeeds (in
`RebuildFluid`, next to `water->SetTexture(surface, 23)`), and clear it whenever the solver is torn
down or unavailable. Without the bit the water renders with its glTF material as a static plane.
Keep `DrawPassMask` as is.

**Acceptance.** `--api gl` and `--api webgpu` scene 7 runs: 0 `[ERROR]` lines, water plane visible
in the back-buffer dump. D3D12/Vulkan/D3D11 unchanged (rings still render).

**Outcome (Stage 2).** Fixed exactly as above via `BackyardScene::SetWaterFluidFeature(bool)`, called
from `RebuildFluid` (true after `SetTexture(surface, 23)`, false on teardown, on `TryInitialize`
failure and on surface-bind failure). Measured: GL and WebGPU scene 7 → **0 `[ERROR]` lines**, both
logging the ordinary-material fallback; D3D12/Vulkan/D3D11 log `FLUID_SURFACE enabled` and 0 errors.
Note the earlier "400 errors in a 400-frame run" was reproduced only after passing `--scene 7`
explicitly — `config.json`'s `"scene": 7` is not honoured on the `--api`-only path, so a bare
`--api gl` run silently tests SandboxScene instead of the Backyard.

---

### W-02 — 32-bit storage for volume fraction and pressure (P1, M, suggested: GLM)

**Done (2026-10-30).** Part 1 `14c705f4` (local agent), part 2 by the reviewer. The staged plan,
section 4 "Stage 5a — W-02", has the measurements.

- **Part 1:** `ComputeStorageFormat::R32Float` exists on D3D11, D3D12, Vulkan, GL and WebGPU (WebGPU
  rejects it loudly: no WGSL storage path for it in this build). Each backend resolves a storage
  format in one helper, and a 3D volume's format is now checked against the shader's declared storage
  format on D3D12 and Vulkan — that mismatch used to be silent garbage. The compute self-test runs the
  same 3D workload as RGBA16F and R32F, and the R32F case writes a value fp16 cannot hold: PASS on
  D3D12, Vulkan and D3D11.
- **Part 2:** α, pressure and divergence are R32F; velocity stays RGBA16F. Still-pool loss 0.693 % →
  0.001 %, the fluid self-test's projection meanX +0.0201 → +0.0066. The three gates that moved were
  not solver physics: the isotropy annulus sat 9 cm off the impact (truncated cell index), gate 5's
  1 mm floor leaned on fp16 noise, and the surface kernel read air trapped beside a floating ball as
  missing water (now counted as volume below two cells). Gate 1 became strict (2 mm, 0.05 %).
  The surface kernel now computes each column once per group (it did so 9×), which with the format
  change took scene 7 from 306.8 to 415.3 fps on D3D12 and 353.4 to 402.3 on Vulkan (720p, same
  session).

**Problem.** Every field is RGBA16F. Near α = 1 a half float has a step of 2⁻¹¹ ≈ 0.00049, so a
per-substep flux smaller than ~0.00024 is rounded away. This is a likely contributor to the
constant ≈ 300-cell loss per 15 frames (see W-03). Pressure (≈ 15 m²/s² at the floor) carries
only ~3 significant digits, which limits how far the projection can converge.

**Fix.** Add a 32-bit single-channel storage format to the compute layer and move α, pressure
and divergence to it.

1. `ComputeStorageFormat` in [BaseDriver.h](../../T850/Framework/include/video/BaseDriver.h):
   add `R32Float`.
2. Registry parser [ComputeKernelRegistry.cpp](../../T850/Framework/src/utils/ComputeKernelRegistry.cpp)
   (`"rgba16float"` branch): accept `"r32float"`.
3. Reflection [ComputeReflection.cpp](../../T850/Framework/src/utils/ComputeReflection.cpp): map
   `SpvImageFormatR32f`.
4. Backends: every place that maps the enum to a native format and validates views —
   `D3D11Compute.cpp`, `D3D12Compute.cpp` (volume creation and the binding validation),
   `VulkanCompute.cpp` (creation and layout check), `GLCompute.cpp`, `WebGPUDriver.cpp`,
   `RenderGraph.cpp` (message only). D3D: `DXGI_FORMAT_R32_FLOAT`. Vulkan:
   `VK_FORMAT_R32_SFLOAT`. GL: `GL_R32F`.
5. `FluidSimulation::CreateVolume` gets a format parameter; α, pressure and divergence volumes
   use `R32Float`; velocity stays RGBA16F.
6. Kernels that write those volumes: `RWTexture3D<float>` with `[[spv::format_r32f]]`; readers
   use `Texture3D<float>`. Update `storage_format` in the registry for every kernel that writes
   them (alpha advect, seed alpha, volume correct, pressure, divergence, clear — the clear kernel
   is used for all volumes, so it needs an R32F permutation or a second kernel).
7. `CS_FluidCopy3D` (volume readback) must handle the R32F source.
8. Extend `ComputeSelfTest` with an R32F volume round-trip on every backend that supports 3D
   compute.

**Pitfalls.** R2 (registry), R3 (format attribute), R6 (staged path). The surface kernel reads α
as `.x`; switching to `Texture3D<float>` changes the load syntax.

**Acceptance.** Compute self-test passes on D3D11/D3D12/Vulkan. Fluid self-test passes. Scene run:
report the `VolumeCorrection` deficit before/after (debug log); report surface rms at t ≈ 11 s.

---

### W-03 — Mass is lost every substep and re-added by a global correction (P1, L, suggested: DeepSeek for the kernel, GLM for the host)

**2026-10-31 — two measurement faults found before the transport work; both fixed.**

1. **`MeasureVolumeFraction` summed 106 304 float32 values in float.** At a total of 83 000 that
   loses ~25 cells to rounding (measured: 82 964 in float against 82 941 in double on the same
   field). Since this function also *locks the seed reference*, the reference was inflated by the
   same amount, so the correction aimed at a number the field could never reach and every deficit
   reading was 25–30 cells high. Now accumulated in `double`. Consequence: the still-pool gate's
   "0.001 % loss" was partly the same rounding; the honest figure is **0.013 %** (gate 1's 0.05 %
   limit stands, ~4× margin; mass start 82 910 → 82 917, drift +0.008 %).
2. **The first α advections lose 30 cells at rest — and it is the projection, not the boundary.**
   Measured by logging the total either side of the advect dispatch: step 1 −21.48 cells, step 2
   −8.14, step 3 +0.00, then constant for the rest of the run; with the advect disabled the total
   stays exactly at the reference. The per-cell diff names the mechanism: the −21.47 is spread over
   **48 112 cells (≈4.5 × 10⁻⁴ each) and exactly 0 cells gain anything**. That is the
   `saturate()` clip: residual divergence makes each cell with net outflow lose α to a neighbour
   that is already at α = 1, where the gain is discarded. An earlier note here blamed
   `AlphaAt()` returning air above the open ceiling — **that was wrong**: `IsSolidCell` is checked
   first and returns 0 flux for every out-of-domain face, so no α can leave that way.

   The proof is the iteration sweep, with everything else fixed (still pool, no ball, no wind):

   | `pressureIterations` | deficit at t = 0.21 s | D3D12 fps, 720p, 0 balls |
   |---|---|---|
   | 60 (the old default) | **+29.6 cells** | 397.1 |
   | 120 (**now shipped**) | **+4.4 cells** | 297.8 |
   | 240 | **+0.1 cells** | 188.9 |

   The transport is conservative **when the velocity field is divergence free**; the loss is created
   by projection residual and destroyed by the clip. Two independent levers follow, and W-03 should
   use both: the per-axis conservative dilation scheme (no clip, so residual divergence cannot
   destroy mass), and more sweeps once W-15 P2 makes them affordable.

   **Acted on 2026-10-31** (`pressureIterations` is now 120 in `WaterTunables`, in
   `Scenes/BackyardWater.json` and in `BackyardSelfTestOptions`, so the gates validate what ships).
   Same-session A/B, 720p, 0 balls: D3D12 **401.9 → 295.4 fps** (`scene.update` 1.53 → 2.45 ms),
   Vulkan **391.6 → 274.2 fps**; with 30 balls 273.0 / 255.5 fps. Still 4–5× a 60 fps budget, and
   the gates improved: still-pool loss **0.013 % → 0.002 %**, mean offset **−0.2 mm → −0.0 mm**,
   `maxAbs` 0.3 → 0.1 mm. The two float gates moved *toward* their limits (`ring−far`
   −4.9 → −5.6 mm against 15, `covered min−ring` −15.9 → −17.3 mm against 20) because a sharper
   projection draws the meniscus around the ball more strongly; W-26 (the trapped-air
   compensation) is what should bring them back.

   **Attempt 1 at the transport itself was measured and rejected** (2026-10-31, nothing shipped —
   the code was reverted). An operator-split, exactly conservative, exactly bounded per-axis scheme
   (three passes per substep, donor/receiver limiter, no `saturate`) is **three times more diffusive**
   than the current unsplit pass: per-column interface width **3.35 → 10.67 cells**, bulk
   0.9969 → 0.9950, same deficit (+53.8, i.e. the displaced body). Three 1D upwind sweeps smear far
   more than one 3D sweep; raising `alphaCompression` to 1.0 made it worse (12.63). Sub-cycling the
   *existing* pass 3× or 6× per substep changed nothing (3.40 / 3.37 cells). What does sharpen the
   surface is solver substeps: `maxSubstepDt` 1/120 → 1/360 → 1/720 gives **3.37 → 3.08 → 2.57**
   cells (bulk 0.9968 → 0.9976 → 0.9984) at 3–6× the whole solver cost.

   **W-03 is therefore re-scoped.** Total α already sits at the body-adjusted reference within
   0.1 cells and the correction is idle, so mass is not the remaining defect: the "diluted bulk" is
   air smeared in by a first-order upwind flux. The acceptance numbers (bulk ≥ 0.999, interface
   1–2 cells) need a **less diffusive flux**, not another conservation scheme.

   **Attempts 2 and 3 (later the same day).** SLIC / Hirt–Nichols donor-acceptor was tried and
   **rejected** (its far-side branch donates nothing when `cfl < 1 − alpha`, so smeared cells freeze:
   bulk 0.9637, interface 7.43, 22 133 cells below 0.98). A **van Leer / minmod limited face value**
   was then adopted: keep the single conservative face flux and replace only the advected value with

   ```
   slope  = minmod(a_donor - a_far, a_receiver - a_donor)
   a_face = clamp(a_donor + 0.5 * slope * (1 - cfl), 0, 1)
   amount = min(cfl * a_face, a_donor)
   ```

   Sharp fronts advect at full cell value; smeared fronts get a reconstruction *below* the average, so
   the profile steepens instead of spreading, and nothing can freeze because the formula is continuous
   in the gradient. Measured at t ≈ 8.2 s after a 4.43 m/s impact (deterministic, D3D12 = Vulkan):
   bulk **0.9969 → 0.9987**, cells below 0.98 **3 998 → 852 (−79 %)**, interface **3.35 → 2.66**
   cells, deficit unchanged (still exactly the displaced body volume). All ten gates PASS on three
   APIs, and the surface gates improved with it: isotropy mirror 1.50/0.72 → **0.01/0.00** mm at
   +0.30 s, float `ring−far` −5.6 → **−1.4…−2.1** mm, `covered min−ring` −17.3 → **−13.8…−15.9** mm.
   Cost is in the noise: same-session A/B on D3D12, 284.6 → **284.9 fps** (`update` 2.632 → 2.604 ms).
   A compression sweep confirms 0.5 stays the best setting. Acceptance: five of seven lines met,
   three marginal (bulk 0.9987 against 0.999, 1.1 % against 1 %, interface 2.66 against 1–2) — all
   pointing at the ~850 cells the splash leaves diluted, which is W-07's and W-26's territory now.
   Details in the staged plan, "Stage 5b, attempts 2 and 3".

**Baseline after those fixes, measured with `--backyard-mass-audit 60` (deterministic; D3D12 =
Vulkan)** — scripted 0.25 m / 39.27 kg ball, 4.43 m/s impact:

| t | total | ref | deficit | bulk mean | bulk min | below 0.98 | interface width (mean / max / columns) |
|---|---|---|---|---|---|---|---|
| 0.21 s | 82 941 | 82 971 | +30.2 | 0.9997 | 0.9991 | 0 of 76 304 | 0.00 / 0 / 0 |
| 1.99 s | 82 946 | 82 971 | +24.8 | 0.9985 | 0.5798 | 1 229 | 2.51 / 13 / 3 013 |
| 3.97 s | 82 907 | 82 971 | +64.1 | 0.9977 | 0.5359 | 2 620 | 2.69 / 13 / 5 000 |
| **8.73 s** | **82 917** | 82 971 | **+53.8** | **0.9965** | 0.7502 | **4 056 of 76 276 (5.3 %)** | **3.49 / 13 / 5 000** |

At 8.73 s the ball floats with `submerged = 0.60`, so the correction's own target is
`ref − 53.8 = 82 917` — i.e. **the correction is holding total alpha exactly at its target**; the
"+53.8 deficit" is the submerged body volume, not lost water. Measure the deficit against the
body-adjusted target, not against the seed value.

**Problem.** Even with the pool at rest, `CorrectVolumeFraction` adds ≈ 300 cells (0.36 % of the
water) every 15 frames. The correction is a stop-gap: it does a full-volume GPU→CPU readback (a
hard stall, see W-04), and it puts water back where there is room, not where it was lost.

**Root cause.** `CS_FluidAlphaAdvect3D` is first-order upwind with `saturate()` at the end. Any
cell whose update would exceed 1 or drop below 0 is clipped, and the clipped amount is destroyed.
Upwind also smears air entrained by the splash through the pool (now limited by the sharpening
term). Half-float storage (W-02) adds rounding loss.

**Evidence from the debug view (review).** The "bulk water" statistic (liquid cells with no air
neighbour, which should all be α = 1) on D3D12 and Vulkan at 1280×720:

| Time | Bulk mean α | Bulk min α | Bulk cells below 0.98 |
|---|---|---|---|
| Before the impact | 0.995 | 0.55 | 204–1765 of ≈ 77k |
| After the impact, steady | **0.96** | **0.89** | **≈ 81 250 of 81 252** |

The whole pool is diluted by about 4 % and never recovers. This is consistent with the clip: the
projection leaves residual divergence, cells with outflow lose α, cells with inflow are capped at
1, so the net is a one-way loss spread through the bulk. The volume correction then adds water
only in the [0.5, 0.999) band, which is now almost every cell, so it refills the bulk instead of
the surface. A correct fix makes the bulk line read ≈ 1.000 and keeps it there after the impact;
add that to the acceptance below.

**Fix (recommended).** Replace the transport with the conservative, bounded, operator-split VOF
scheme of Weymouth & Yue (2010, *J. Comput. Phys.* 229, "Conservative Volume-of-Fluid method for
free-surface simulations on Cartesian-grids"):

* Advect one axis per pass (X, Y, Z; alternate the order every substep), each pass a separate
  dispatch with ping-pong.
* Each pass uses face fluxes from a simple geometric interface reconstruction (or, as a first
  step, the existing upwind/sharpened flux) **plus** the dilation term `dt · c · ∂u/∂x`, where
  `c = 1` if the cell's α at the start of the step is ≥ 0.5, else 0. This term makes each 1D pass
  exactly conservative and keeps α in [0,1] under the 1D CFL condition, so no clipping is needed.
* Keep the correction as a safety net but log its deficit; it should become ~0.

**Alternative (smaller).** Keep the unsplit scheme but replace `saturate` clipping with flux
limiting: compute each face flux, then scale outgoing fluxes of a cell so it cannot go below 0 and
incoming fluxes so it cannot exceed 1 (Zalesak-style limiter). Fluxes stay antisymmetric, so the
domain sum is exact.

**Pitfalls.** The dilation term must use the **stored** face velocities (MAC), same as the flux.
Solid faces carry zero flux. Mind R5 (no local arrays).

**Acceptance.** Debug log: `VolumeCorrection deficit` < 20 cells per 15 frames over 3000 frames
(at rest **and** after the impact). Fluid self-test mass drift line within ±0.05 %. Surface rms at
t ≈ 11 s not worse than 2.5 cm. Rings still render. Debug view "bulk water" mean α ≥ 0.999 and
fewer than 1 % of bulk cells below 0.98, before and 10 s after the impact.

---

### W-04 — GPU→CPU readbacks stall the frame (P2, M, suggested: GLM)

**Status: done** (`c911381a` steps 1, 2, 4; `ad7e019e` step 5; `4e93d9d5` step 3).
`CS_FluidVolumeReduce3D` writes the sums and `CS_FluidVolumeCorrect3D` computes the deficit itself,
so the live correction reads nothing back (only `--logLevel debug` reads the 64-byte sums). The
probes run only when info logging is on.

Step 5, the asynchronous readback, is the API W-06 needed:

```cpp
int  RequestComputeReadback(ComputeBuffer& buffer, size_t bytes);  // ticket, or -1
bool ConsumeComputeReadback(int ticket, void* out, size_t bytes);  // false until retired
```

Both default to `-1` / `false` in `BaseDriver`, so a backend without the pair keeps compiling and
the caller falls back to the synchronous `ReadComputeBuffer`. D3D12 keeps a READBACK-heap resource
per frame slot plus the frame's list; Vulkan uses a host-visible VMA staging buffer per frame slot.
**The pitfall is frame identity:** offscreen compute frames do not rotate the back buffer, so
`m_currentBackBuffer` is pinned and `m_frameFencesValues[slot]` is overwritten every frame — the
slot index can never say "this copy has been submitted". D3D12 therefore tracks `m_frameSerial`
(incremented in `CompleteFrame`), stores it in the ticket, and stamps the fence value onto every
pending copy of the frame being completed, before the fence array is reused. Measured cost:
under 1 % at 720p with 0 and 30 balls (`4e93d9d5`).

Step 3 is done differently from the plan: the diagnostics buffer is created once, and because
there is still no buffer-write API, `CS_FluidDiagnostics3D` clears its own two `uint4` counters
from thread (0,0,0) when `Iteration == 0xFFFFFFFF`, dispatched as a pass before the measuring one.
The mass audit is the check that the clear happens: `div rms` decays monotonically as the pool
settles (0.504 at t = 1.9 s → 0.0037 at t = 18.3 s) instead of growing every frame.

**Problem.** `CorrectVolumeFraction` calls `MeasureVolumeFraction` twice (before and after), and
each copies the entire volume (110 000 × 16 bytes) to a buffer and reads it back synchronously.
`BackyardScene::OnDraw` additionally calls `ReadDiagnostics` (allocates a new buffer every call)
and `ReadVolumeFraction` every 60 frames. Each readback is a full CPU/GPU synchronisation, so the
frame time spikes every 15 frames and the real-time step (`deltaSeconds`) becomes uneven.

**Fix.**
1. New kernel `CS_FluidReduce3D`: one pass that sums α, liquid-band headroom (α ∈ [0.5, 0.999)),
   and liquid-band α into a small RW buffer using `InterlockedAdd` on fixed-point values (the
   pattern `CS_FluidDiagnostics3D` already uses). Read back 16 bytes instead of 1.76 MB.
2. Drop the "after" measurement in `CorrectVolumeFraction` unless `--logLevel debug`.
3. Persist the diagnostics buffer instead of creating it per call.
4. Gate the scene's per-60-frame probes (`[Backyard][surface]`, `[columns]`, `[fluid]`) behind a
   dev-GUI checkbox or `--logLevel debug`; keep them available because the acceptance runs in this
   document rely on them.
5. Optional (larger): an asynchronous readback (fence-tracked ring of readback buffers, result
   consumed 2–3 frames later). This needs a new `BaseDriver` API; if attempted, implement for
   D3D11/D3D12/Vulkan and keep the synchronous path as a fallback.

**Acceptance.** Same deficit/rms numbers as before (the correction must behave identically).
Report frame-time max and p99 over a 3000-frame run before and after, using `--profile
--profileFrames 3000` or the PresentMon workflow in
[gpu-performance-profiling-workflow.md](../rendering/gpu-performance-profiling-workflow.md).
Done: 1 200 profiled frames per API, 0 spikes, p99 within 0.4 ms of the baseline.

---

### W-05 — Moving the ball re-uploads the wall mask synchronously (P2, M, suggested: DeepSeek)

**Status (`c911381a`): done another way.** The mask is now a persistent read-write buffer;
`CS_FluidStampBodies3D` rewrites it on the GPU from a static base mask plus an inline body list
whenever a footprint changes. No mask buffer is created after startup and nothing waits. The
analytic-body design below is kept for reference only; revisit it only if W-07 needs it.

**Problem.** `UpdateWallMaskForBody` stamps the ball into a CPU copy of the mask and calls
`CreateComputeBuffer` with initial data whenever the ball's cell footprint changes — up to 12
times per frame while it moves. On D3D12 each call creates a committed resource, an upload
buffer, a command list, and waits for the GPU (`WaitForQueuedUploadWork`).

**Fix.** Make the body analytic everywhere instead of stamping it:

* `IsSolid(cell)` in every fluid kernel becomes "static mask **or** `IsBodyCell(cell)`". This
  requires the `ObjectConstants` cbuffer (b1, 12 floats) in: advect, forces, divergence,
  pressure, extrapolate, alpha advect, surface. Register b1 for each in the registry (R2) and pass
  it from `DispatchVolume` (R7) and `StepStage` (R6).
* `UpdateWallMaskForBody` then only uploads the static mask once (keep `m_baseMask` for the CPU
  diagnostics).
* The surface kernel must **not** count body cells as "solid under water" (that would raise the
  surface over the submerged ball); use the static mask only there.

**Pitfalls.** The surface kernel already uses b1 for `GeometryConstants`; the body constants need
a different register there (b2) or the geometry values must move into the object block. R1 when
combining the two tests.

**Acceptance.** Debug log shows no "Fluid Wall Mask created" lines after startup. Scene metrics
unchanged within noise (cavity depth at impact ≥ 0.4 m, ball at rest submerged 0.60). Self-test
passes.

---

### W-06 — The ball does not feel the simulated water (P2, M, suggested: GLM)

**Problem.** `AdvanceSphere` computes buoyancy from the **authored** water plane, not the
simulated surface, and receives no force from the fluid. The ball cannot ride waves, and the
energy it should lose by radiating waves is replaced by an artificial heave damping ratio
(`sphereHeaveDamping`, default 0.15).

**Fix.**
1. Each substep (or each frame), compute the local water level under the ball on the GPU: average
   the **surface texture** over a ring just outside the ball's footprint (radius r + 1.5·h), using
   a small reduction kernel. Since W-19 the texture's covered texels already hold the inpainted
   free-surface level, so the footprint gives the same answer; do not re-derive the level from
   alpha columns, which still stop at the body's underside. Read it back asynchronously (W-04
   step 5) or once per frame with a 1-frame delay.
2. Use that level instead of `waterPlaneY` for the cap volume in `AdvanceSphere`.
3. Optional: horizontal drift. Average the liquid face velocities in a shell around the ball and
   apply drag relative to that velocity instead of to zero.
4. Re-tune `heaveDamping` downwards once the ball reacts to the real surface; document the new
   default.

**Acceptance.** Ball at rest: submerged 0.60 ± 0.02. Drop a second time with "Drop now" while
waves are still running: the ball's `y` in the log must correlate with the local surface (show
both series). No new stalls (W-04 metric).

**Status: done** (`4e93d9d5`), steps 1 and 2. `CS_FluidBodyLevel.hlsl` averages the surface
texture over an annulus around body 0 — inner radius 1.05 r (outside the ball's own meniscus),
outer 2 r + h (inside the next body) — and `AdvanceOneSphere` floats on
`waterPlaneY + average`. Notes:

* One output slot per thread group, so there are no atomics and no zeroing: the buffer needs no
  clear between frames, which matters because there is no buffer-write API. Sums are fixed point
  at 1/1024 m so the readback is an integer ring average (32-bit sum, 32-bit count per slot).
* The kernel is dispatched `(width, depth, 1)`; the grid row is `id.y`, **not** `id.z`. Using
  `id.z` silently averaged row 0 only and looked plausible.
* The surface texture stores a **delta** from the authored plane (`height = elevation −
  WaterPlaneY`), so the absolute level is the plane plus the sampled average. Without the add the
  ball sank to the floor.
* The value arrives one to three frames late through W-04 step 5 and is held between arrivals; if
  a backend has no async path the query pays one 40-byte stall rather than losing the feature.

Measured: with the ball floating it settles oscillating around `y` = −0.05 … −0.08 m and
`submerged` 0.54–0.63 (gate 6 still reports 0.60), so it now rides the surface instead of sitting
on the authored plane. Steps 3 (horizontal drift) and 4 (retune `sphereHeaveDamping`) are still
open — that 0.15 ratio is now the only hand-tuned term standing in for the wave radiation the ball
should produce.

**Step 3 — the body is carried by the current (2026-10-31).** `CS_FluidBodyLevel` now also averages
the horizontal liquid velocity in the same annulus, at the shallower of the column's waterline row
and the body's own centre row, and returns it as two more fixed-point integers in the same slot
(the buffer is `uint4`: Σheight, Σcount, Σu, Σw, all at 1/1024). `AdvanceOneSphere` applies drag to
`(v − flow)` and adds the flow back:

```
vx = flowX + (vx − flowX) / (1 + a_drag/|v_rel| · dt)
```

so a current carries the body asymptotically instead of the water being an absolute brake. Cost:
one extra texture fetch in a 91-group dispatch; same-session A/B at 720p with 0 balls, D3D12
**310.1 → 312.5 fps**, Vulkan **291.9 → 295.5** (inside run variance, no regression).

Verified two ways. (a) A 30-ball run produces a real current (`flow` reaches +0.05…+0.13 m/s) and
the body drifts with it. (b) A direction check with the flow forced to (+0.5, −0.25) in the
integrator for one build: the body crossed to `x = +4.249` (the +X wall) and `z = −8.750` (the −Z
wall), i.e. exactly the forced direction; the temporary override was then removed. That check was
needed because **the wind tunable turns out to be inert** — see W-27.

**Step 4 — heave damping is measured, not guessed.** With the body now floating on the real
surface, the question was whether 0.15 can come down. It cannot, and the reason is structural: the
coupling is one-way, so nothing radiates the body's heave energy away, and the damping ratio is the
only sink. Scene 7, `--backyard-ball-probe 30`, settled window t = 5…10 s:

| `sphereHeaveDamping` | y range in the settled window | y min … max | max \|vy\| |
|---|---|---|---|
| **0.15 (shipped)** | **18 mm** | −0.083 … −0.065 m | 0.050 m/s |
| 0.05 | 128 mm | −0.145 … −0.017 m | 0.365 m/s |
| 0.00 | 307 mm | −0.225 … +0.082 m | 0.875 m/s |

At 0.15 the residual 18 mm is the body following the pool's remaining motion (the sampled level
sits within 4 mm of the plane over the same window), not a free oscillation. 0.15 is also inside the
0.1–0.3 band a real heaving sphere radiates at, so it stays, and it is now a justified number rather
than a fudge factor. A two-way coupling (the body pushing α out of its cells, i.e. W-07) is what
would let it come down.

---

### W-07 — Water under the ball is deleted, not displaced (P2, M, suggested: DeepSeek)

**Problem.** When the ball's footprint moves, newly covered cells become solid and the alpha
kernel writes them to 0. That water disappears and is only restored, spread over the whole pool,
by the next volume correction. The rim around the entry point is therefore lower than it should
be.

**Fix.** Before stamping a new footprint (or, with W-05, before advecting α each substep), run a
displacement kernel: for each liquid cell that is a body cell in the new configuration but was
not in the previous one, push its α into the nearest non-body neighbours, preferring the
direction of the body's velocity and then upwards. The kernel needs the previous and current body
constants. Conserve the moved amount exactly (scatter with integer atomics into a buffer, or
gather from the receiving side).

**Acceptance.** Debug log: the correction deficit during the 0.3 s after impact drops by at least
half compared with the baseline. Impact rim height (max of `[columns]` in the first 0.3 s after
impact) increases.

**Attempt 1 (2026-10-31) — implemented, measured, reverted.** A gather pass
(`CS_FluidDisplaceBody3D`) ran after the mask restamp and before the α advection: every free cell took
an equal share of each adjacent *body* cell's water, split by how many free faces that body cell has,
so the covered cells were emptied into their neighbourhood instead of being zeroed by the transport.
It conserved what it moved (up to the receiver's headroom), needed no previous mask and no atomics,
and cost one dispatch per substep while a body was enabled.

Measured, same binary and settings, D3D12 = Vulkan:

| | t = 2.19 s | | t = 8.13 s | |
|---|---|---|---|---|
| | no pass | with pass | no pass | with pass |
| bulk mean | 0.9996 | 0.9995 | 0.9987 | 0.9987 |
| cells below 0.98 | 114 | **368** | 852 | **941** |
| interface width | 1.65 | 1.61 | 2.66 | 2.71 |
| deficit | +8.0 | +8.0 | +53.8 | +53.8 |

So the *global* numbers are unchanged — as they should be, since the volume correction already
restores the displaced volume — and the local effect is below the audit's resolution. What it did do
is **fail gate 8**: "the surface near the body flickers by 0.0339 m between frames (limit 0.010)",
because a floating ball's footprint flickers as it bobs across cell boundaries and each flip moved a
whole cell of water into one neighbour. The still-pool isotropy deltas also flipped sign at +0.30 s
(+0.0083 → −0.0068 m).

**Next design, if this is taken up again:** distribute upward- and velocity-biased instead of equally
(as the fix above originally said), weight by distance so a single cell flip spreads over several
receivers, and run it only on the substeps where the footprint actually changed — then re-check gate 8
before anything else, because that gate is what caught this. The mass argument for the pass is
weaker than it looked: the correction already holds total α at the body-adjusted reference, so the
prize is the *local* rim, and that needs a probe that can see it (the audit cannot).

**Attempt 2 (2026-10-31) — the design was analysed and the cheap route rejected before writing it.**
The probe this item needed now exists (`--backyard-ball-probe N`, see the hand-back in the staged
plan), so the local rim is finally measurable: the settled float leaves a **−2.0 mm** ring offset
and a **−15.9 mm** covered-column dip against the far field, with a 10 cm band profile of
−0.9 −1.0 −0.8 −0.2 cm. Attempt 2's plan was "displace only when the footprint changed, weighted by
distance". Analysed, that cannot work:

1. **The mask flip is the quantisation, not the distribution.** A body cell holds α = 0 after the
   transport, so a gather pass only ever picks up water from cells that *just* became solid — attempt
   1 was already "on change" in effect. Its whole-cell flicker came from the *binary mask*, not from
   re-moving water: the ball's centre crosses a cell boundary, the mask flips, and one full cell
   (0.73 L) changes hands in a single substep regardless of how the amount is then spread. Weighting
   by 1/distance reduces the *peak* per receiver only linearly, so gate 8's 10 mm limit is still
   exceeded for realistic flip rates (a floating ball crosses a boundary about twice a second).
2. **Sub-cell coverage cannot be bolted onto the displacement pass alone.** The excluded volume that
   matters is `α · (coverage(t) − coverage(t−dt))`, but `coverage` is only meaningful if the *mask
   itself* is a solid fraction: `IsSolid` in seven kernels, the wall-mask encoding of the body index
   (`2u + k`), and the transport's zeroing rule all assume a bit. Passing an analytic sphere to one
   extra pass changes the water that moves without changing what the surface integral reads, so the
   moat stays.

So the real task is Qwen's suggestion in the review of this attempt: **replace the body's binary
mask with a per-cell solid fraction** — a `solidFraction` volume, `freeVolume = 1 − solidFraction`,
α measured per unit free volume, an analytic sphere-cube coverage for the body, and every kernel's
`IsSolid` reading the fraction (or a threshold plus the fraction for the transport). Then a 5 mm bob
changes a cell's solid fraction by 5.5 % instead of flipping it, the displaced volume per substep is
smooth, and gate 8 is satisfied by construction. That is a stage, not a patch: seven kernels, the
mask encoding, the stamp kernel and the surface kernel's body-column logic. It is scoped here and
deliberately not started in the same session as the probes it would need.

**What would have to be true to accept it:** the same band profile at the settled float (currently
−0.9 −1.0 −0.8 −0.2 cm) flattens toward 0 with the far field, `covered min−ring` shrinks from
−15.9 mm without the trapped-air term (which is what W-26 wants), gate 8 stays under 10 mm, gate 6
stays 0.60 ± 0.02, and the volume correction's deficit is unchanged.

---

### W-08 — The domain ceiling is a lid (P2, S, suggested: DeepSeek)

**Problem.** The domain ends 0.42 m above the water plane and every kernel treats out-of-range
cells as solid, including above the top row. A strong splash (try density 3.0, drop height 2.5 m)
hits a solid lid instead of open air.

**Fix.** Treat `j ≥ Height` as AIR, not SOLID, in the pressure (ghost value 0 → Dirichlet),
gradient and extrapolation kernels, and treat the top faces of the last row as AIR faces.
Keep zero α flux through the top so water cannot leave the domain (it falls back). Alternatively
raise `freeboard` in `build_backyard.py` and the layout JSON; report the memory and time cost.

**Acceptance.** Heavy-drop run (density 3.0, height 2.5 m): no α accumulation in the top row
(probe the top row of the ball column), self-test passes.

**Outcome (Stage 2) — implemented, and this acceptance criterion is wrong.** The fix that shipped is
a **setting**, not a global change: `FluidSettings::openCeiling` (default `false`), opt-in from
`WaterTunables::openCeiling` (true for the pool), branching on a new `OpenCeiling` cbuffer field in
`CS_FluidPressure3D` and `CS_FluidGradient3D`. Making it global is not safe — see below.

* **Only two kernels needed changing.** Divergence does **not** read out of range: on this staggered
  layout the +Y face of cell `(i,j,k)` is texel `(i,j,k).y`, already in range, so there is no `j+1`
  texel. The lid came entirely from the gradient kernel's `IsSolid(b)` branch overwriting the face
  velocity with `SolidBoundaryValue() = 0`. Extrapolate must **not** be changed either: it runs after
  the gradient and classifies the top face as solid, which means "do not extrapolate", so it preserves
  the value the gradient just wrote. Treating it as an AIR face would overwrite it with a neighbour
  average.
* **Pressure uses `diag += 1` (theta forced to 1), not a theta-derived ghost.** The row above the
  domain has no stored alpha; when the top row is fully wet there is no air cell to compute theta
  against, and the 0.1 clamp would over-soften the diagonal and checkerboard at the lid. `diag += 1`
  is a plain Dirichlet p = 0 and also keeps the matrix non-singular.
* **Why it cannot be global.** The engine's synthetic `--fluid-selftest` (a 32×16×16 closed box,
  unrelated to the pool) failed with `velocity points into a wall face (max 0.630859 m/s)`. An open
  top is a vent, so a closed-box test must keep it closed.
* **R2 cost.** `FluidConstants` grew 64 → 80 bytes and the registry's per-binding `constant_count`
  had to go 16 → 20 in all 16 fluid entries **at once**. With the struct grown and the registry left
  at 16 every compute pipeline failed to create, silently, and the solver reported only "resources are
  incomplete".

**Why the acceptance criterion cannot be met as written.** Measured on the real pool grid, a
3x-density 0.25 m ball dropped 2.5 m reaches `top-row alpha = 1.000` and a peak column rise of
`+0.419 m` — the full 0.42 m of freeboard — **identically with the lid and without it**. That ball
displaces ~90 cells into a 1.56 m deep pool, so it genuinely overflows it, and the closed domain has
nowhere for the water to go. Filling the top row is therefore *correct*; pinning against it while
venting is not permitted is the actual defect. The gate therefore probes the **+Y face velocity of a
wet top-row cell**: open ceiling gives `0.7939 m/s` (PASS), a closed ceiling gives `0.0000 m/s`
exactly (FAIL). If the pool must not overflow, raise `freeboard` — that is a layout/`W-11` change,
not a solver change.

**Review correction (2026-10-28).**

* **Ghost-pressure mismatch, fixed.** The pressure kernel solves the open-ceiling face with
  θ = 1 (ghost pressure 0), but the gradient kernel fell through to `PressureForFace`, which derives
  θ from α. For a full top row that gives θ = 0.5 and a ghost pressure of −p, so the gradient
  applied twice the pressure step the solve assumed. The gradient now uses 0 for that face, matching
  the pressure kernel. Measured top-face |v| max: **0.7939 → 0.1478 m/s**; gate 6 still passes
  (threshold 0.01 m/s) on D3D12 and Vulkan. Most of the "venting velocity" above was the mismatch.
* **"The pool genuinely overflows" is wrong.** The ball displaces about 90 cells = 0.065 m³; spread
  over the 40.5 m² pool that raises the level by **1.6 mm**. The +0.419 m is the local splash crown
  at the impact rim, which reaches the top of the domain because α still cannot leave through the
  top face. That needs an extreme drop (3× density from 2.5 m); the default drop does not reach the
  top row.
* **Freeboard decision.** Keep 0.42 m. Raising it to ≈ 1 m takes the grid from 22 to about 29 Y
  rows, roughly 30 % more cells on every substep, to serve a stress test. Expose `freeboard` as a
  data-driven value in W-11 instead, so a scene that wants big splashes can opt in.

---

### W-09 — Water optics use constant depth and unguarded refraction (P2, M, suggested: GLM) — DONE in `517ae6bf`

**Problems** in the `FLUID_SURFACE` block of [FS_Mesh.hlsl](../../T850/Assets/Shaders/FS_Mesh.hlsl):

1. **Depth for absorption.** `columnDepth = waterToFloor + R`, with `waterToFloor` a constant
   1.56 m. Over the steps the real depth is 0.26–0.86 m, so the steps are tinted like deep water.
   Fix: compute a static per-column floor height once (CPU, from the static mask) into a small
   R16F/R32F 2D texture bound at a free slot, and use `surface − floor` as the depth.
2. **Refraction picks up foreground objects.** The refracted lookup into `SceneColorTex` has no
   depth test, so the ball above the water and the coping bleed into the water. Fix: sample
   `SceneDepthTex` at the refracted UV; if that depth is nearer than the water fragment, fall back
   to the unrefracted UV.
3. **Fresnel ignores the waves.** It uses `eyeDir.y` (flat normal). Use the solver normal blended
   towards the flat normal (e.g. 50 %) so crests change reflectivity without the dark patches the
   original author reported.
4. `RenderMesh::Draw` binds textures through per-slot branches: a new slot needs a new branch
   (see the implementation record, bug 18).

**Acceptance.** Back-buffer dumps before/after from the same snapshot (`--replaySnapshot`) at
t = 2.0 s: steps visibly lighter than the deep end; no ball colour around the ball in the water.
Zero errors on D3D12/Vulkan/D3D11.

---

### W-10 — The water is invisible to depth-based post effects (P3, M, suggested: GLM) — DONE (redesigned in review, `47500166`)

**Problem.** The water is excluded from the GBuffer pass (`DrawPassMask`) so that SSR can see the
pool floor. As a result DOF, SSAO, fog and god rays treat the pool as empty: objects under water
get ambient occlusion and focus as if they were in air.

**Fix.** After the forward water pass, write the water's depth into a separate depth target (a
depth-only draw of the displaced mesh) and let the post passes that need scene depth use the
nearer of the two. Measure the cost of the extra draw.

**Acceptance.** Depth target dump shows the water surface; frame time cost reported.

**What was done.** No extra draw was needed. "Forward Transparent" continues the `Deferred` target
(`push: false`, contents loaded) with `depth_stencil: READ_WRITE`, and `Deferred`'s depth is cleared
at the "Deferred" pass and never touched by opaque geometry (the lighting pass is a full-screen
quad). So after the forward pass `Deferred:DEPTH` holds exactly the visible water — the shader's
own `discard` against `GBuffer:DEPTH` already removes water behind the coping and ball. Measured
against Stage 3b's separate `WaterDepth` pass: 110,770 / 110,770 overlapping pixels identical on
D3D12 (109,904 on Vulkan); the separate pass only added 9–12k pixels of water *hidden* behind
opaque geometry, cost ≈ 0.18 ms re-shading the water, and depended on `mesh_indices: [5]`.

Consumer: the CoC pass (`FS_Quad` `COC_PASS`, HLSL, GLSL and WGSL) samples
`max(slot 0, slot 1)` — reverse-Z, so the max is the nearer surface — for the pixel depth and for
auto-focus. Backyard binds `GBuffer:DEPTH` at 0 and `Deferred:DEPTH` at 1. DayScene and Minecraft
bind `GBuffer:DEPTH` at both (`max(z, z) = z`, unchanged). Every graph that runs CoC **must** bind
slot 1: WebGPU rejects a declared-but-unbound texture ("Sampled texture missing"), and Vulkan's
dummy texture is white (1.0 = nearest under reverse-Z), which would put every pixel in the near
field. A/B with DOF on and aperture 5, deterministic frame 600 on D3D12: CoC-near changes in 30,870
of 512×512 texels, exactly over the pool, mean +36 levels; CoC-far unchanged; back buffer changes
in 44,740 pixels inside the pool, max 5 levels. Default config is unchanged (Backyard ships with
`dof_enabled: false`, and its `aperture: 90` saturates CoC everywhere — tune both before turning
DOF on). The `RT_Dump_WaterDepth.ppm` dump now reads `Deferred:DEPTH`; dump it with DOF off,
because the DOF pass re-pushes `Deferred` and clears it before the end-of-frame dump.

Not covered, by design: SSAO ("Shadow Accumulation") runs **before** the water is drawn, and the
Backyard graph has no fog or god-ray pass that reads depth after it. A future post pass that needs
the water should take the same two inputs.

---

### W-11 — Tunables are not data-driven; grid size can disagree with cell size (P2, M, suggested: GLM)

**Problems.**
1. About 90 water tunables live as C++ defaults in
   [WaterTunables.h](../../T850/Framework/include/scene/WaterTunables.h); nothing loads them from
   the scene JSON (the implementation record already flags this as the largest gap).
2. `alphaCompression` (0.5), `maxSubstepDt` (1/120) and the volume-check interval (15) are not
   tunables and not in the GUI.
3. `gridWidth` (100) and `gridDepth` (50) are literals. The wall mask places cells using
   `basin size / width`, while the kernels use `cellSize`. Changing `cellSize` in the GUI without
   changing the grid makes the two disagree silently. The cell-size slider also has no effect
   until "Rebuild grid" is pressed.
4. `WaterGpuParams` comment says "Fifteen float4 groups"; there are 24.

**Fix.** Derive width and depth from the layout like the height already is
(`deriveGridFromLayout`), so only `cellSize` is a free parameter; rebuild automatically when it
changes. Add the three solver values to `WaterTunables` and the GUI. Load `WaterTunables` from a
`"water"` object in `BackyardScene.json` with glaze (`error_on_unknown_keys = true`, matching the
engine's strict JSON policy), falling back to the C++ defaults.

**Done in review: item 3.** `FluidSimulation::DeriveGrid(PoolExtent(), cellSize)` derives all three
counts from one cubic cell size. The size is snapped so X is a whole number of cells, preferring a
width within ±2 that also makes Z whole (a 2:1 pool snaps to even widths), and the basin max
X/Y/Z become exact multiples of the cell, so the mask stamp, the kernels and the debug renderer
agree. In the GUI the "grid width (X cells)", "grid depth (Z cells)" and "cell size (m)" sliders
are linked views of that one value (0.03–0.30 m). The derived height, cell count and a cost
estimate are shown below them, and the grid rebuilds when the slider is released. Measured:

| Requested cell | Grid | Still pool at 2 s (mean / rms) |
|---|---|---|
| 0.09 (default) | 100×22×50 @ 0.0900 | −10.5 mm / 0.1 mm (unchanged; all 8 gates PASS D3D12+Vulkan) |
| 0.06 | 150×33×75 @ 0.0600 | −3.0 mm / 0.5 mm (W-11 acceptance met) |
| 0.075 | 120×26×60 @ 0.0750 | −0.4 mm / 2.3 mm |
| 0.12 | 74×16×37 @ 0.1216 | −0.4 mm / 3.2 mm |

Items 1, 2 and 4 (JSON loading, the three solver tunables, the stale comment) remain open.

**Traps (found in the Stage 3 review).** `WaterTunables::floorTextureBound` is runtime state that the
scene sets when the floor texture binds; keep it out of the JSON metadata. `WaterTunables::freeboard`
is overwritten by `pool.freeboard` from `Models/Backyard/backyard_layout.json`, so the tunable has no
effect; keep the layout as the single source of truth.

**Acceptance.** Editing a value in the JSON changes the run (show one example). Setting
`cellSize` to 0.06 rebuilds a 150×33×75 grid automatically and the pool still sits at the authored
plane (surface rms at rest < 3 mm).

---

### W-12 — Self-test gaps (P1, M, suggested: either)

The self-test missed every bug fixed in e90752b4. Add these gates (a new `--backyard-selftest`
running the real scene headless, or extend `--fluid-selftest`):

1. **Still pool.** Ball disabled, 2 s of simulation: mean speed < 0.01 m/s, surface rms < 3 mm,
   mean surface delta within ±5 mm of the authored plane, step columns within ±1 cm.
   (Would have caught the 9 cm offset, the step hole, the start-up test seed and the free-falling
   air.)
2. **Isotropy.** Ball dropped in the middle of the pool: compare the surface delta at equal
   distances along ±X and ±Z at t = 0.3 s; difference < 20 %. (Would have caught the `id.z` bug
   that made waves travel along X only.)
3. **Mass.** Ball enabled, 10 s: correction deficit per check < 0.1 % of the water (tightened
   after W-03).
4. **Moving boundary.** A ball moving at constant velocity through still water: liquid faces next
   to it carry its normal velocity; no liquid face points into it relative to its velocity.
5. **Readback.** Read texels from the surface texture at several positions (the old probe read
   only texel (0,0)), e.g. via `CS_FluidCopy3D`-style copy to a buffer.

A CLI switch to disable the ball is needed for gate 1 (implemented as `--backyard-no-ball`).

**Acceptance.** All gates pass at e90752b4 (gate 3 with the current 0.5 % threshold). Each gate
must **fail** when the corresponding bug is reintroduced: demonstrate by temporarily reverting the
`id.y` fix and the wall-mask shell fix, then restore them.

---

### W-13 — Dead code (P3, S, suggested: DeepSeek)

* `CS_FluidSphere3D.hlsl`, `FluidSimulation::InjectSphere`, `m_removedBuffer`,
  `m_removedColumns`, `BuildSpreadConstants` (refers to a non-existent `CS_FluidSphereSpread3D`)
  and the `fluid_sphere` registry entry are unused. `CS_FluidSphere3D` also has the `id.z` row bug
  the surface kernel had.
* `FluidSettings::pressureScale`, `initialFill` and `wetThreshold` are unused.
* `FluidSimulation::Reset` only resets two indices and is unused.

Remove them (and the `fluid_sphere` pipeline creation in `TryInitialize`). Keep the divergent
`Seed` permutation: the self-test uses it.

**Acceptance.** Build, self-test and scene run unchanged.

**Outcome (Stage 2).** All of the above removed, plus the `fluid_sphere` pipeline creation and its
`complete` check. `CS_FluidSphereSpread3D.hlsl` (referenced by the `SpreadConstantsBlob` comment) no
longer exists, so that struct went too. Registry 26 → 25 kernels. Build, `--fluid-selftest` (both
APIs), `--backyard-selftest` (both APIs) and the scene are unchanged.

---

### W-14 — Duplicated constants across kernels (P3, S–M, suggested: either)

The `FluidConstants` cbuffer is copied into 16 kernels, `ObjectConstants` into 3, and
`T8_LIQUID_THRESHOLD` into 5 kernels plus `FluidSettings::airThreshold` in C++. A change to one
copy silently diverges from the others.

**Fix.** First check whether the compute compile path resolves `#include` (pipelines are created
from source text read by `ResourceLocator`; DXC and glslang need an include handler). If it does,
move the shared declarations into `Shaders/FluidCommon.hlsli`. If not, pass the threshold through
the constants (`Padding0` is already per-kernel, so add it to `ObjectConstants` or a new field) and
add a test that compares the cbuffer text of all fluid kernels.

**Acceptance.** Self-test and scene run unchanged; one place to change the threshold.

---

### W-15 — Solver and frame cost (P1, M–L) — P1, P4, P5, P6, P7, P8 done; P2, P3 and P9 measured and not worth it

**Performance pass 2026-10-31 — four of the seven open items landed, two rejected on measurement.**
Same-session A/B on D3D12 and Vulkan at 720p with `telemetry.ps1`: **0 balls D3D12 283.7 → 298.6 fps,
Vulkan 264.0 → 280.6 fps**; 30 balls 268.6 / 259.8. Per item:

| Item | Result |
|---|---|
| **P7** surface written in place | Removes the ping-pong copy dispatch and its barrier every frame |
| **P8** unused render-graph passes | Torch Particles removed with its target; the merge pass became a plain copy of Deferred. Debug cascade passes kept (they already early-out in-shader) |
| **P4** Vulkan descriptor-set cache | Keyed by layout + bound identities + constant bytes, cleared with the pool; transitions still run on a hit. Vulkan 275.9 → 282.8 fps |
| **P5** D3D12 pipeline state | Heaps/root signature/PSO only set when they change, trackers no longer nulled after a dispatch. +0.5 % |
| **P2** in-place red-black pressure | **Rejected.** Needs one volume as both sampled input and storage output: D3D11 corrupts (96.9 % of the water lost), D3D12 hangs. Built the combined-state + alias opt-in for D3D12/Vulkan and reverted it with the rest |
| **P3** batched D3D12 barriers | **Rejected.** One `ResourceBarrier` per dispatch measured 301.1 → 298.5 fps |
| **P9** direct texture→readback | Not attempted: debug paths, never per frame |

**`pressureIterations` 60 → 120 (2026-10-31).** Conservation is tied to projection convergence, so
the shipped sweeps doubled (W-03's note has the sweep table). Same-session A/B at 720p with
`backyard-diag\telemetry.ps1`, 0 balls: **D3D12 401.9 → 295.4 fps** (`scene.update` 1.526 → 2.445 ms),
**Vulkan 391.6 → 274.2 fps** (`update` 1.287 → 2.338 ms). With 30 balls: D3D12 273.0, Vulkan 255.5,
0 spikes. The cost is ~0.92 ms of solver recording per frame on D3D12 — the per-dispatch fill/drain
of 60 extra pressure sweeps, which is exactly what P2 (in-place red-black on R32F) targets: halving
the dispatch count is what buys the sweeps back.

**Measured** (review, 2026-10-28; RTX 4080 Laptop, x64 Release, scene 7, 1280×720,
`--logLevel info`; median of four 600-frame windows after the drop, from the timestamps of the
`[Backyard][timing]` lines; script `D:\Code\QwenFlashT850\backyard-diag\perf.ps1`):

| Configuration | D3D12 | Vulkan |
|---|---|---|
| Baseline (`b9eabc40`) | 191 fps (5.24 ms) | 298 fps (3.36 ms) |
| Solver not stepped (render only) | 457 fps (2.19 ms) | 798 fps (1.25 ms) |
| → solver share of the frame | **3.05 ms (58 %)** | **2.10 ms (63 %)** |
| Saved without the volume correction | 1.01 ms | 0.67 ms |
| Saved with 20 instead of 60 pressure iterations | 1.21 ms | 0.89 ms |
| Saved without the verification re-read | 0.45 ms | ≈ 0 |
| Saved without the 60-frame scene probe | 0.43 ms | ≈ 0 |
| CPU time inside `Step()` (stall sources off) | 0.37 ms | 1.3 ms |
| Render graph, all passes (profiler GPU time) | ≈ 1.0 ms | — |
| **After `3a371903`** | **219 fps (+15 %)** | **313 fps (+5 %)** |

Reading: D3D12 is **GPU-bound** in the solver (0.37 ms of CPU but 1.9 ms of extra frame time);
Vulkan is **CPU-bound** (≈ 10 µs to record each of ≈ 130 dispatches). Each correction or probe
readback is a full GPU stall (≈ 15 ms and ≈ 26 ms on D3D12) amortised over 15 or 60 frames. The
render graph is cheap at 720p: shadow depth 0.29 ms, shadow accumulation 0.21 ms, water 0.17 ms,
GBuffer 0.15 ms, everything else ≤ 0.04 ms.

**Done in review (`3a371903`, results unchanged, all gates PASS):**
1. The surface kernel and its copy run only on the frame's last substep (they ran every substep).
2. The re-read that verified each volume correction (a second full readback and stall) runs only
   at `--logLevel debug`; it only fed a debug line.
3. The scene's 60-frame probe (diagnostics and surface readbacks) runs only when info logging is
   on; the user's `config.json` uses `error`.

**Open, in priority order:**

| # | Finding | Where | Fix | Expected |
|---|---|---|---|---|
| P1 | Volume correction is a synchronous readback of the whole α volume as RGBA (16 B/voxel, 4× what is needed) plus a CPU sum, every 15 frames. D3D12 `ReadComputeBuffer` also creates a new committed readback resource per call and calls `WaitForGPU` | `MeasureVolumeFraction`, `D3D12Driver::ReadComputeBuffer` | **Done in `c911381a`**, without any readback: GPU reduction plus a GPU-side deficit (see the profiling pass below) | ≈ 1.0 ms D3D12, 0.7 ms Vulkan; measured: a 10–15 ms stall every 15 frames removed |
| P2 | Pressure: 60 red-black iterations = 120 full-grid dispatches per substep. Half the threads only copy the other colour through (write-only storage + ping-pong) | `CS_FluidPressure3D`, `StepInternal` | **In-place red-black attempted 2026-10-31 and rejected**: it needs the same volume as both sampled input and storage output, which D3D11 corrupts (96.9 % of the water lost) and D3D12 hangs on. A combined-state + alias opt-in was built for D3D12/Vulkan and reverted with the rest. Remaining options: fewer iterations with a logged residual, several sweeps per dispatch in groupshared tiles, or MGPCG | up to ≈ 1.2–1.8 ms D3D12 (not reachable in place) |
| P3 | D3D12 barriers: every dispatch issues a transition SRV→UAV, a UAV barrier, then a transition UAV→SRV, each in its own `ResourceBarrier` call | `D3D12Driver::DispatchCompute` | **Batched 2026-10-31 and reverted**: one `ResourceBarrier` call per dispatch measured 301.1 → 298.5 fps, i.e. no gain, matching the earlier note. The UAV barriers and the SRV↔UAV transitions themselves are still per resource; batching them is not worth the complexity | measured: no gain |
| P4 | Vulkan records ≈ 10 µs per dispatch (1.3 ms per frame) because it allocates and writes a descriptor set every call | Vulkan compute dispatch | **Done 2026-10-31**: the finished set is cached for the frame, keyed by layout + every bound identity + the constant bytes, cleared with the descriptor pool. Layout transitions still run on a hit. Vulkan 275.9 → 282.8 fps (720p, 0 balls) | measured: +2.5 % Vulkan |
| P5 | D3D12 CPU per dispatch: six `unordered_set`s and a vector allocated, `dynamic_cast` per binding, keep-alive map insert per resource, `SetDescriptorHeaps`/root signature/PSO rebound even when unchanged (the pressure PSO 120× in a row) | `D3D12Driver::DispatchCompute` | **Partly done 2026-10-31**: the heaps, root signature and PSO are only set when they change, and the trackers are no longer nulled after a dispatch (a graphics draw still rebinds, because a compute PSO never equals a graphics one). The per-dispatch sets and `dynamic_cast`s remain | measured: +0.5 % D3D12; the rest matters once the GPU side is fixed |
| P6 | The moving body re-creates the whole wall-mask buffer (copy of 110k words, new GPU buffer, upload) whenever its cell footprint changes: every substep while it falls or bobs | `UpdateWallMaskForBody` | **Done in `c911381a`**: a persistent read-write mask restamped on the GPU (`CS_FluidStampBodies3D`) | measured: a ≈ 10 ms stall per footprint change removed |
| P7 | The surface kernel no longer reads its previous output, so the surface ping-pong pair and the copy dispatch are unnecessary | `StepInternal`, `PublishSurface` | **Done 2026-10-31**: the kernel writes the stable output texture directly when one is bound; the pair remains only for the headless/self-test path, where nothing binds an output | part of P7+P8: D3D12 283.7 → 299.5, Vulkan 264.0 → 275.9 fps |
| P8 | The render graph carries template passes the backyard never uses | `BackyardScene_RenderGraph.json` | **Done 2026-10-31**: Torch Particles removed with its render target; the Light Add pass that merged it became "Transparent Composite", a plain copy of Deferred into Extra16F (what the HDR chain needs, since transparencies are already drawn into Deferred). The two debug cascade passes stay: they already early-out in-shader and removing them deletes an owner-facing debug view | part of P7+P8: D3D12 283.7 → 299.5, Vulkan 264.0 → 275.9 fps |
| P9 | Readbacks go texture → compute copy → structured buffer → copy → readback heap | `ReadVolumeFraction`, `ReadVelocity`, `ReadSurface` | Not attempted: debug paths only, and the audit runs on a cadence (`--backyard-mass-audit`), never per frame | debug paths; low |

Checked and fine: the whole solver runs in compute shaders (no pixel-shader simulation); the
post-processing passes that have compute versions already use them; the 3D volumes need both UAV
and SRV access, which is what they have; GBuffer, shadows and the water draw are cheap at 720p.

**Profiling pitfalls.** The engine profiler records scopes only while its frame is open, and
`FluidSimulation::Step` runs in `OnUpdate` before that, so GPU scopes placed inside the solver are
silently dropped. Use frame-rate A/B with `perf.ps1`, or move the profiler's frame start before
`OnUpdate`. In review the Vulkan `--profile --profileFrames 1200` run never exited (killed at
300 s); D3D12 exits normally.

**The Vulkan numbers above no longer reproduce on this machine (2026-10-29).** `perf.ps1`, same
settings, rebuilt with the PCH cleared each time:

| Tree | D3D12 | Vulkan |
|---|---|---|
| HEAD (`5511e7e9`, Stage 4) | 216 fps | 176 fps |
| `7bd0afe9` (before Stage 3c/4) | — | 173–178 fps |
| `945b2f7c` (before Stage 3) | — | 171–183 fps |
| `3a371903` (measured **313** on 10-28) | — | **172 fps** |

So the drop is environmental, not a code regression, and D3D12 is unaffected. Vulkan is CPU-bound
in it (0.88 of a core busy against 0.43 for D3D12; GPU 56 % busy, P0, no throttle reasons). Ruled
out: implicit Vulkan layers (OBS hook and all implicit layers disabled → no change), presentation
(`--benchmarkNoPresent` → 175 fps), CPU clocks (boosting, AC, "Performance" plan), P-core pinning,
background load. Not ruled out: the machine has not rebooted since 2026-10-12, and the NVIDIA driver
logged an error (event 153) at 2026-10-28 21:31 during Thunderbolt/display changes, two hours after
the 313 fps run. Re-measure after a reboot before trusting any Vulkan figure.

**Resolved 2026-10-30:** the same `perf.ps1` run measures Vulkan **315 fps** and D3D12 **220 fps** at
HEAD (`97009aef`, 0 spawned balls), so the drop was the machine state, not the code.

**Rule for every performance claim:** never compare against a number in this document. Measure the
parent commit and the change in the same session (`backyard-diag\bisect_perf.ps1 -Commit <sha>
-Apis vulkan,d3d12 -Restore` builds the old tree, measures with `perf.ps1` and restores HEAD).

**Acceptance (per item).** Before/after table from `perf.ps1` (median of the four windows) on
D3D12 and Vulkan; `--backyard-selftest` and `--fluid-selftest` PASS on both; the scene metrics in
2.4 unchanged.

**Profiling pass 2026-10-30 (reviewer, `c911381a`): three CPU–GPU stalls removed.** Measured with
the engine's runtime telemetry, not FPS alone: `backyard-diag\telemetry.ps1` runs the scene
`--unattended` at log level `error` (the owner's setting; `perf.ps1`'s `info` level adds a
60-frame readback probe) and reports per-frame medians of `scene.update` (solver recording),
`frame.draw`, `gpu.gpu_wait` (CPU blocked on the GPU), plus mean fps, p99 and spike count. Median
fps alone hid the periodic stalls.

| 1280×720, 0 balls, same session | D3D12 mean fps (p99) | Vulkan mean fps (p99) |
|---|---|---|
| Before | 222 (8.8 ms), 1 spike | 322 (10.9 ms), 45 spikes / 600 frames |
| After `c911381a` | 292–305 (4.4 ms), 0 spikes | 344–361 (3.7–4.0 ms), 0 spikes |

1. **D3D12 `UpdateFloatData` waited on the GPU** (new allocator, list, fence and event per call,
   then a blocking wait). The tiled deferred-light pass uploads two float textures per frame, so
   3.1 ms of every D3D12 frame was this wait (`render.pass.Deferred Light Volumes` 3.11 → 0.15 ms).
   Inside a frame the copy is now staged in the per-frame ring and recorded in the frame's command
   list, like Vulkan. Same-API A/B on a deterministic DayScene frame: byte-identical. Engine-wide:
   every scene with point lights on D3D12 benefits.
2. **P1 done differently: the volume correction never reads back.** `CS_FluidVolumeReduce3D` sums
   total, interface headroom and interface alpha on the GPU (fixed-point atomics, two slots), and
   `CS_FluidVolumeCorrect3D` derives the deficit and its guards itself. The old full-volume readback
   stalled one frame in 15 (10–15 ms). Mass gate unchanged.
3. **P6 done: the wall mask is restamped on the GPU.** A persistent read-write mask is rewritten by
   `CS_FluidStampBodies3D` from a static base mask plus an inline body list whenever a footprint
   changes; the old path re-created the buffer through a blocking upload (~10 ms). Gate 9
   identical. The scene's 600-frame timing line also no longer reads the volume back unless info
   logging is on.

**Where the frame goes now.** Both APIs are GPU-bound (`gpu.gpu_wait` 1.0–1.7 ms). The solver's
≈127 small dependent dispatches dominate: removing 60 pressure dispatches (60 → 30 iterations)
saved 0.56 ms (D3D12) and 0.70 ms (Vulkan), ≈9–12 µs of GPU time per dispatch on a 90k-cell grid,
so the cost is per-dispatch fill and drain, not arithmetic. Batching D3D12 barriers (part of P3)
measured no gain and was reverted. The next lever is P2 (in-place red-black on R32F after W-02,
dispatching only the active colour, then fewer or fused sweeps). Vulkan still spends ≈1.5 ms of
CPU recording those dispatches (P4).

---

### W-16 — The implementation record is out of date (P2, S, suggested: either)

[backyard-water-simulation.md](backyard-water-simulation.md) still describes the collocated
solver (sections 6–8), the old surface contract details, the old bug list (14) and measurements
(16). Rewrite those sections from section 2 of this document and the e90752b4 commit message, and
add the bugs fixed in that commit to the root-cause log. Keep the historical bugs; mark the ones
whose fix was replaced.

**Acceptance.** No statement in the record contradicts the code (spot-check: storage layout,
pressure convention, surface dispatch, ball model, volume correction band). Also record the
open-ceiling setting (W-08) and the covered-column inpainting in the surface kernel (W-19) in
section 7 of the record.

---

### W-17 — Untested platforms (P3, M, suggested: either)

* Android (Vulkan) and Linux/Steam Deck: the solver uses 3D storage images and compute on every
  substep; neither platform has run scene 7.
* ARM64 Windows: builds only Framework + DayScene; not run.
* The fallback path on GL/WebGPU depends on W-01.

**Fix/acceptance.** Run scene 7 and the fluid self-test on each available platform, record
results in the implementation record, and file a task for each failure.

---

### W-18 — Vulkan descriptor pool exhausted in headless compute loops (P1, S) — DONE

Added during Stage 0 by the implementer: a headless dispatch loop never reaches `CompleteFrame`, so
the per-frame descriptor pool was reset once and ran dry (`VK_ERROR_OUT_OF_POOL_MEMORY`) at step 109
of a 400-frame self-test. Fixed in `a18d34e5` with `RecycleOverflowingComputeFrame`, which submits,
waits and recycles only frames opened by the compute path once 75 % of the pool is used. Interactive
frames use a few hundred sets and never reach the threshold. Reviewed and accepted.

---

### W-19 — The floating ball sits in a funnel of water (P1, M) — DONE in `fa5422ff`

**Symptom.** At rest the ball looked like a weight on a stretched fabric: the water surface bent
down into a dark collar around it, and reflections and refraction warped towards it.

**Root cause.** The water simulation was right; the rendered surface was reconstructed wrongly
around the ball.

1. `ColumnElevation` in [CS_FluidSurface3D.hlsl](../../T850/Assets/Shaders/CS_FluidSurface3D.hlsl)
   counts solid cells only when wet water lies above them. In a column through a floating ball
   nothing wet lies above the ball, so the column reported the water level **under** the ball
   (≈ −34 cm). The mesh vertices inside the footprint dove to the ball's underside.
2. The 3×3 tent filter in the same kernel and the bilinear reconstruction in `VS_Mesh` spread that
   dip 1–2 cells beyond the waterline, where the mesh is visible: a funnel.
3. The funnel walls were steep (slopes clamp at ±4), so refraction, glint and reflections bent
   strongly there: the "lensing" look and the dark collar.

The scene's `[columns]` log hid part of this: it re-derived the surface from alpha with the
**static** mask (no ball), so its numbers did not match what was drawn.

**What was implemented** (differs from the plan proposed in review, for the reasons given). The
first two bullets describe `fa5422ff`; the body test was later replaced, see the flicker follow-up
below for the current kernel:

* `RawColumnElevation(x, z, out state)` classifies each column as `COLUMN_DRY` (no water / out of
  range), `COLUMN_FREE`, or `COLUMN_COVERED`. A column is covered when a solid run starts directly
  on a wet cell (α ≥ 0.5) and no wet cell follows above it. This is read from the wall mask the
  kernel already binds, so **no `ObjectConstants`, registry or C++ binding change was needed**, and
  it works for any body stamped into the mask, not just the sphere.
* A falling body (air below it) and a submerged body (wet water above it) are **not** covered, so
  both keep the previous behaviour.
* `ColumnElevation` returns the raw value for free columns; for covered ones it walks outward in 8
  directions (up to `INPAINT_REACH = 8` cells) to the first non-covered column and averages the
  free ones found. The tent filter calls `ColumnElevation`, so heights and slopes stay consistent.
* `BackyardScene`'s `[surface]`/`[columns]` log now reads the real texture via `ReadSurface`.
* New self-test **gate 7** (`BackyardSelfTest.cpp`): drop the default ball from 1 m, settle 8 s,
  read the surface texture, compare the ring `[r + 0.5h, r + 1.5h]` and the covered texels
  (`dist ≤ r − 0.5h`) with the far-field median (1.0–1.5 m). Limits: ring within 1.5 cm of far
  field, covered texels at most 2 cm below the ring. Logs a 10 cm-band radial profile.

**Measured** (gate 7, profile in cm vs far field for bands 0–0.1, 0.1–0.2, … m):

| | ring − far | covered min − ring | profile | Gate |
|---|---|---|---|---|
| Before (D3D12) | −2.27 cm | −33.8 cm | −36.0 −29.1 −11.5 −1.7 −0.4 −0.3 | FAIL (2) |
| After D3D12 | −0.61 cm | −0.34 cm | −0.8 −0.8 −0.8 −0.5 −0.4 −0.3 | PASS |
| After Vulkan | −0.81 cm | −0.27 cm | −1.0 −1.0 −0.9 −0.7 −0.4 −0.3 | PASS |

Scene 7 at t = 10.7 s: rendered surface min −2.1 cm (was −35 cm), `[columns]` flat within ±1 cm
across the ball's row; back-buffer shows the ball on a flat surface with no collar. Fluid and
backyard self-tests PASS on D3D12 and Vulkan; scene 7 on D3D11 runs with 0 errors.

**Residual, not a defect yet.** The surface within ~0.3 m of the ball sits within ±1 cm of the
far field. Gate 5's off-centre drop still finds its extreme at x = 56 (unchanged). If W-07
(displacement) changes this, recalibrate gates 7 and 8 from measurement and note it here.

**Follow-up: flicker while the ball settles.** The `fa5422ff` kernel decided "covered" with an
α ≥ 0.5 test on the cell under the body. While the ball bobbed, that cell's α crossed 0.5 every few
frames, so columns flipped between the inpainted level and the body's underside: up to **0.44 m**
frame-to-frame next to the ball, seen as flickering in the water (simulation, not reflection). The
kernel now uses `ReadColumn()`, which detects a body from the **wall mask only** (a solid run
resting on an open cell; steps stand on the floor and never match), and blends continuously from the
ring level to the water lying on top of the body (`lerp(ring, bodyTop + waterAbove·h,
saturate(waterAbove))`), so a submerging body has no switch either. New **gate 8** measures the
largest per-texel second difference |h(t) − 2h(t−1) + h(t−2)| within r + 3h of the ball from 3–4 s
after the drop (limit 1 cm):

| | Gate 8 jerk | Gate 7 ring − far | covered min − ring |
|---|---|---|---|
| `fa5422ff` kernel (D3D12) | **0.4445 m** at 0.06 m from the ball — FAIL | — | — |
| Mask-only kernel D3D12 | 0.0066 m — PASS | −0.12 cm | −0.63 cm |
| Mask-only kernel Vulkan | 0.0067 m — PASS | −0.19 cm | −0.71 cm |

Scene 7 on D3D12 and D3D11: 0 errors. Fluid self-test PASS on both APIs.

**Rules learned** (apply to any later surface-kernel change):

* Diagnostics must read the texture the shader draws (`ReadSurface`), not a CPU re-derivation.
* Never switch a surface texel between two reconstructions on a thresholded, time-varying field
  (α ≥ 0.5); classify from the mask and blend continuously. A single settled-frame gate cannot see
  this — gate 8 samples consecutive frames.
* Gates 7 and 8 must keep failing on a broken kernel: restore `fa5422ff`'s shader and expect gate 8
  to fail; make `ColumnElevation` return `own.elevation` unconditionally and expect gate 7's two
  `float:` errors.

---

### W-20 — `config.json` scene index ignored without `--config` (P3) — RESOLVED, by design

Confirmed in review: a bare `DayScene.exe` (with or without `--api`) starts SandboxScene with the
helmet. `App.cpp` loads a config file **only** when `--config <path>` is passed
(`FindConfigArgument` → `LoadRuntimeConfig`); otherwise the built-in defaults apply. The Launcher
passes `--config`, so its runs honour `"scene": 7`. No code change.

**Rule for every command in these documents:** always pass `--scene 7` explicitly. A run without it
tests SandboxScene and logs no `[Backyard]` lines — check that the log contains `[Backyard]` before
trusting any result.

---

### W-21 — Vulkan fluid self-test fails after the Backyard scene initialises (P2, S–M, suggested: GLM)

**Problem.** `--scene 7 --api vulkan --fluid-selftest` fails every time; under every other scene
tested it passes, and D3D12 passes either way. Found in the Stage 3 review; it is **not** caused by
Stage 3 — the same failure reproduces with all Stage 3 files reverted to `945b2f7c`.

| Configuration | Runs | Result |
|---|---|---|
| scene 7, Vulkan, HEAD | 8 | 8 FAIL: mass drift −99.92 % (×2) or −18.21 % (×6) |
| scene 7, Vulkan, HEAD, floor texture skipped | 5 | 5 FAIL: −0.03 % (projection ratio 1.056) or −27.28 % |
| scene 7, Vulkan, Stage 3 reverted (`945b2f7c`) | 5 | 5 FAIL: −0.03 % or −27.28 % |
| scene 0, Vulkan, HEAD | 5 | 5 PASS, −0.02 % |
| scene 1 (DayScene), Vulkan, HEAD | 3 | 3 PASS, −0.02 % |
| scene 6 (Minecraft), Vulkan, HEAD | 3 | 3 PASS, −0.03 % |
| scene 7, D3D12, HEAD | 8 | 8 PASS, −0.02 % |

The outcome jumps between a few discrete values, and in the worst runs the projection *increases*
divergence (ratio 418, then mass 7168 → 6) — corrupted state, not numerical noise. The self-test
runs in `main()` right after `OnCreateApplication`, before the frame loop, so the Backyard solver
has been **initialised (resources created, volume fraction seeded) but never stepped**. An earlier
version of this entry blamed "two solvers stepping together"; that was wrong. `--backyard-selftest`
(on the scene's own 100×22×50 grid) passes after the same initialisation; the fluid self-test's
32×16×16 grid fails.

**Fix.** Vulkan compute allocates descriptor sets and constant slices per dispatch, and each
`FluidSimulation` owns its pipelines, so look at **per-frame** driver state that Backyard's
initialisation leaves behind: an open frame and who opened it (`m_frameStarted`,
`m_frameOpenedByCompute`), the descriptor pool and CB ring of that frame, and barriers or layout
transitions for newly created 3D images. The three diagnostics to run first are in section 0, step
1 of the staged plan. Fix the driver; do **not** "fix" it by making the harness skip the scene. If
it turns out to be harness-only (no shipped path can create a solver and then dispatch outside a
frame), document why and make the harness drain the scene's frame before the self-test.

**Acceptance.** `--scene 7 --api vulkan --fluid-selftest` PASS 5/5 with mass drift within ±0.1 % and
projection ratios matching the scene-0 run; scenes 0, 1, 6 and D3D12 results unchanged;
`--backyard-selftest` PASS on both APIs.

**Outcome (Stage 3c) — RESOLVED, and the diagnosis above was wrong about the cause.** It is not
per-frame Vulkan state at all: it is **uninitialised GPU volume memory**.

Diagnostics run (as section 0 ordered): the debug log shows **no** `Recycling the compute-owned
frame` line (so the descriptor pool never runs dry) and a temporary driver diagnostic proved **no**
compute dispatch ever ran inside a renderer-opened frame (so frame ownership is not the cause). The
`App.cpp` drain of the scene's frame gave **3 FAIL / 2 PASS in 5 runs** — inconclusive, and it
exposed that the failure is **intermittent**, not 8/8: the outcome is either PASS at −0.00 % or a
total loss (`7168 → 6 cells`, −99.92 %).

Root cause: the self-test seeds alpha and then steps, but `FluidSimulation::TryInitialize` left the
velocity, pressure and divergence volumes holding whatever was in GPU memory. Under scenes 0/1/6
that memory happens to be zero, so nothing showed. After the Backyard solver has created and seeded
its own 100×22×50 volumes, the allocator hands back *those* contents, and the first step begins from
garbage momentum — divergence grows ×540 in one pressure iteration. `--backyard-selftest` always
passed because it calls `ResetToRest()` first, and its own source comment had already documented
this exact hazard.

Fix: `TryInitialize` now calls `ResetToRest()` once the solver is ready, so **every** caller starts
from a defined state. In the solver, not the harness — any caller that creates and steps a solver
must get defined initial state. The temporary harness drain and driver diagnostic are reverted.

Measured after the fix: scene 7 Vulkan **6/6 PASS** (drift −0.00 %), and 6/6 again with the drain
removed, so the fix is the clear and not the drain; scenes 0, 1, 6, 7 × Vulkan and D3D12 **8/8 PASS**
(`meanSpeed` 0.1086 / 0.1083); `--backyard-selftest` PASS both APIs with Stage 2 numbers unchanged;
scenes 0, 1, 6, 7 on five APIs → **0 errors in all 20 combinations**.

Section 4.2 no longer needs `--scene 0` for the fluid self-test: it now passes under any scene.

---

### W-22 — Spawn more balls at random places, sizes and weights (P2, M–L, suggested: GLM for the host, DeepSeek for the kernels)

**Request.** A GUI button that drops another ball at a random position that always lands in the
pool, with a random radius and a random weight. The scripted ball and its drop stay as they are.

**Why this is not just a GUI change.** The solver knows exactly one body: `FluidSimulation` holds a
single `FluidSphere m_sphere` (`DroppedSphere()`), `UpdateWallMaskForBody` stamps that one sphere,
`AdvanceSphere` integrates it, the volume correction subtracts its submerged volume, and the
gradient and obstacle-velocity kernels read its centre, radius and velocity from the 12-DWORD
`ObjectConstants` block (b1). Every one of those must become a list.

**Constraint that decides the design (measured).** D3D12 binds compute constants as root constants,
and a root signature holds 64 DWORDs in total. `CS_FluidGradient3D` already uses 38: b0 20 + b1 12 +
four descriptor tables + one root SRV (see the `[D3D12][Compute] Root parameter` lines at info
level). Storing centre, radius and velocity per body (8 DWORDs) therefore caps the list at 4 bodies,
and there is no API to update a compute buffer every substep (only `CreateComputeBuffer` with
initial data, which is the synchronous upload W-05 complains about).

**Design — put the body index in the wall mask.** The mask is one `uint` per cell and every kernel
and CPU reader only tests `!= 0u` / `== 0u` (checked: all 10 fluid kernels, `FluidDebugRenderer`,
`BackyardSelfTest`, `BackyardScene`). Keep static solids at `1u` and stamp body *k* as `2u + k`.
Then:

* `IsSolid` is unchanged everywhere.
* `IsBodyCell(cell)` in the two kernels becomes `WallMask[idx] >= 2u`, and the body's velocity is
  `BodyVelocity[WallMask[idx] - 2u].xyz`. Centre and radius are no longer needed on the GPU, because
  the stamp already encodes the footprint. `ObjectConstants` becomes `uint BodyCount; uint3 pad;
  float4 BodyVelocity[kMaxBodies];` = 4 + 4·N DWORDs. With **kMaxBodies = 8** the gradient root
  signature is 20 + 36 + 6 = 62 DWORDs — under the limit. Confirm with the log line, and if a kernel
  gains a binding later, the cap must shrink with it.
* R1 (no short-circuit in HLSL 2018): guard the index before using it —
  `uint k = (m >= 2u) ? min(m - 2u, kMaxBodies - 1u) : 0u;` then branch on `m >= 2u`.
* The surface kernel's covered-column detection (W-19, gates 7 and 8) is mask-only and keeps working
  for any number of bodies.

**Host changes.**
1. `FluidSimulation`: replace `m_sphere` with `std::vector<FluidSphere> m_bodies` (capacity
   kMaxBodies). Keep `DroppedSphere()` returning `m_bodies[0]` so the scene, `BackyardSelfTest` and
   gates 7/8 stay as they are. Add `AddBody(const FluidSphere&)` (returns the index or -1 at the
   cap), `RemoveBodiesFrom(size_t first)` and `BodyCount()`.
2. `AdvanceSphere` → loop over bodies. Add sphere–sphere contact (push the pair apart along the
   centre line by the overlap, then exchange the normal velocity with `restitution`); without it two
   balls stamp the same cells and the later one wins. Floor and wall collision already exist per
   body.
3. `UpdateWallMaskForBody`: stamp every enabled body with `2u + k`, in index order. The dirty check
   must compare all bodies' cell AABBs, not one. Each change still re-uploads the whole mask (W-05):
   measure the cost with 1 and with 8 moving balls and report it — that measurement is W-05's
   motivation.
4. Substep count: use the fastest body (`bodySpeed` is currently from one sphere).
5. Volume correction (`reference -= ballCells * submerged`, around line 740): sum over bodies.
6. `BuildObjectConstants`: header + one velocity per body slot; registry entry sizes for both
   kernels in `compute_kernels.json` must match the new cbuffer (R2).

**Scene changes (`BackyardScene`).**
1. Rendering: one `PrimitiveInst` per extra ball, sharing the ball primitive already loaded for
   `Backyard_Ball.glb` (create with `CreateInstance(PrimitiveMgr.GetPrimitive(ballPrimitive), ...)`,
   add with `m_renderContainer.AddMeshInstance`, remove with `RemoveMesh`). Keep the handles in a
   vector parallel to the bodies; weld transform and scale to each body every frame, as is done for
   `m_ballHandle`.
2. Tunables in `WaterTunables` **and** the `glz::meta` in `BackyardWaterJson.cpp` (otherwise the
   strict loader rejects the new JSON keys): `spawnMaxBalls` (≤ kMaxBodies − 1), `spawnRadiusMin` /
   `spawnRadiusMax` (defaults 0.10 / 0.35 m; clamp the minimum to ≥ 1.2 × `cellSize`, a smaller ball
   is not resolved by the grid), `spawnDensityMin` / `spawnDensityMax` (defaults 0.2 / 1.6 × water,
   so some float and some sink), `spawnHeightMin` / `spawnHeightMax` above the water plane (0.5 /
   2.0 m), `spawnSeed` (0 = random each run; non-zero = reproducible).
3. Placement that always lands in the pool: take the basin from the solver settings, never from
   literals (`basinMinX/MaxX/MinZ/MaxZ`). Draw `x ∈ [minX + r + h, maxX − r − h]`, `z` likewise
   (h = one cell of margin), `y = waterPlaneY + r + U(heightMin, heightMax)`, horizontal velocity 0,
   so it falls straight down inside the walls. Reject and redraw (up to 32 tries) if the new ball's
   footprint overlaps an existing ball's (distance < r₁ + r₂ + h); give up with a log line rather
   than spawning inside another ball. Landing on the steps counts as in the pool.
4. Weight: radius and density ratio are drawn independently; mass is derived
   (`density · 1000 · 4/3 π r³`). Log each spawn: index, position, radius, density, mass in kg.
5. GUI, in "Dropped sphere": **Spawn ball** (disabled when the solver is not running — GL and
   WebGPU — or at the cap, with the reason shown), **Clear spawned balls**, and a line
   "balls: N / max". "Reset simulation" also clears spawned balls.
6. Optional: a per-instance tint so balls are told apart; the ball primitive is one dark-red
   material, so this needs a per-instance colour or brightness — check `PrimitiveInst` before adding
   anything.

**Acceptance.**
* Default run unchanged: `--backyard-selftest` (all 8 gates) and `--fluid-selftest` PASS on D3D12
  and Vulkan with the same numbers as before; scene 7 0 errors on five APIs.
* With `spawnSeed` fixed, spawn 7 balls: every ball's final `x, z` lies inside the basin minus its
  radius (log the final positions after 10 s); no ball overlaps another; balls with density < 1
  float with submerged fraction ≈ their density (± 0.05), the rest rest on the floor or steps.
* Mass: the correction deficit stays within the range of the single-ball run.
* Performance: fps with 0, 1 and 7 extra balls, same session (`perf.ps1`), and the count of mask
  re-uploads per second at info log level.
* The debug view's "body voxels" count rises with each ball.

**Pitfalls.** Changing `ObjectConstants` without updating the registry size breaks the dispatch on
D3D12 ("passed N bindings, pipeline expects M") — R2. A stale PCH after editing
`FluidSimulation.h` silently keeps the old struct (Stage 3c). Do not touch `CS_FluidSurface3D`: gates
7 and 8 guard it.

**Later (with W-05).** When the body becomes analytic instead of stamped, the list moves to a
structured buffer updated through a new upload API, and the 8-body cap goes away. *(Done early, in
`97009aef` — see "Uncapped" below.)*

**Outcome (Stage 4c) — DONE.** Mask `2u + k`, `ObjectConstants` 12 → 36 DWORDs (registry updated in
the same commit), `kMaxBodies = 8`, body list with contact resolution, per-body stamping with a
per-body footprint check, fastest-body substeps, volume correction summed. Scene: shared ball
primitive, one instance per body welded each frame, basin-derived placement with 32 rejection tries,
8 new tunables in the meta (109 keys), GUI Spawn/Clear, `--backyard-spawn N` test hook. New gate 9
(two bodies, different velocities): `body0 cells=47 maskErrors=0 worst=0.00024`, `body1 cells=21
worst=0.00015` (mask 2 and 3), PASS both APIs. Measured: 7 balls spawned inside the basin with
radii 0.13–0.25 and densities 0.36–1.54; spawnSeed 12345 reproduced identical placement across two
runs; fps 159.4 / 152.7 / 143.7 for 0 / 1 / 7 extra balls (same session); 20/20 scene×API
combinations 0 errors; frame-300 dump shows the balls at different sizes and depths.
Not done from the spec: the optional per-instance tint (item 6), and the "submerged fraction ≈
density" and "body voxels count" acceptance lines were not measured explicitly — the gate 9 and fps
numbers are the evidence recorded here.

**Follow-up fixes by the reviewer (2026-10-30).**

* `9f6e7a2f` — **balls and their ripples were mirrored in Z.** Not a body bug: the water mesh's UV
  ran opposite to world Z, so the water drew the solver mirrored about the pool's centre line, where
  the scripted ball drops. Fixed in the generator; details in the staged plan, section 4, "Water
  mirrored in Z".
* `97009aef` — **no ball cap.** The owner asked for any number of balls.
  * GPU: per-body velocities moved out of `ObjectConstants` into `StructuredBuffer<float4>
    BodyVelocity : register(t9)` in `CS_FluidGradient3D` and `CS_FluidObstacleVelocity3D` (Vulkan
    bindings 7 and 5). `ObjectConstants` (b1) is now a 4-DWORD header holding `BodyCount`. Registry:
    b1 `constant_count` 36 → 4, plus a `read_only_buffer` at t9. The D3D12 root signature no longer
    grows with the body count.
  * Engine: a `ReadOnlyBuffer` compute binding may carry **inline data** instead of a persistent
    buffer (`ComputeBindingDesc::inlineData / inlineBytes / inlineStride`). The backend uploads it for
    that dispatch only: D3D12 from the per-frame upload ring as a root SRV (upload-heap memory is
    `GENERIC_READ`); Vulkan from the CB ring, which now also has `STORAGE_BUFFER` usage; D3D11 as an
    immutable structured buffer + SRV (the same per-dispatch pattern its constants use). GL and
    WebGPU reject a null buffer, as before; the solver does not run there. Use this for any small
    per-dispatch array instead of growing a root-constant block.
  * Host: `kMaxBodies` is gone; the footprint cache is a vector. `ResolveBodyContacts` keeps the
    exact all-pairs loop up to 32 bodies, so existing runs are unchanged. Above 32 it uses a
    uniform-grid broad phase (cell = largest diameter, candidates in ascending index, so it stays
    deterministic).
  * Scene: `spawnMaxBalls` removed (tunable, meta 109 → 108 keys, JSON, GUI). `--backyard-spawn N` is
    no longer clamped. New **Spawn 10 balls** button. When no free spot is found after 32 tries, the
    ball is dropped from above whatever it would touch instead of the spawn failing.
  * Measured: seeded 4-ball frame 600 **byte-identical** to the previous build; **100 balls (101
    bodies)** run 1200 frames with 0 errors on D3D12, Vulkan and D3D11, all landing in the pool.
    Self-tests PASS on D3D12 and Vulkan with unchanged numbers, gate 9 included. Scene 7 logs 0
    errors on all five APIs; T8ditor builds. fps at 720p, same session (`perf.ps1`):

    | Spawned balls | D3D12 | Vulkan |
    |---|---|---|
    | 0 | 220 | 315 |
    | 7 | 199 | 300 |
    | 100 | 141 | 222 |

    The cost is per-ball draws plus the whole wall-mask re-upload on every footprint change, which is
    exactly W-05's target.

---

### W-23 — Textured materials with normal maps (P2, M, suggested: GLM)

**Problem.** Every Backyard mesh is a flat colour: `build_backyard.py`'s `material()` sets only
Base Color, Metallic and Roughness factors. The engine already supports full glTF PBR textures —
`GLTFMaterial.cpp` maps `baseColorTexture`, `metallicRoughnessTexture`, `normalTexture` (with
`scale`), `occlusionTexture` and `emissiveTexture`, embedded (`bufferView`) or external images,
plus `KHR_texture_transform`; tangents are generated with MikkTSpace when the file has none
(`GLTFMesh.cpp`). So this is an asset task, not an engine task.

**Source: ambientCG (CC0).** All assets are CC0 1.0 — free for any use including commercial, no
credit required ([license](https://docs.ambientcg.com/license/)). Download pattern, confirmed
through the public API: `https://ambientcg.com/get?file=<ID>_1K-JPG.zip`. Each zip holds
`<ID>_1K-JPG_Color.jpg`, `_NormalGL.jpg`, `_NormalDX.jpg`, `_Roughness.jpg`, usually
`_AmbientOcclusion.jpg` and `_Displacement.jpg`. **Use `NormalGL`** — glTF normal maps are OpenGL
convention (+Y up); `NormalDX` makes every bump look inverted. Poly Haven (polyhaven.com, also CC0)
is the alternative. Candidates that exist with a 1K-JPG download (API query, 2026-10-29) — pick by
looking at the previews on `https://ambientcg.com/a/<ID>`:

| Surface (material in `build_backyard.py`) | Candidates |
|---|---|
| House walls, chimney (`House_Stucco`) | Plaster001, Plaster002, PaintedPlaster017 |
| Yard wall (`Wall_Adobe`) | Plaster007, Plaster003 |
| Roof (`House_RoofMetal`) | CorrugatedSteel005, CorrugatedSteel009 |
| Patio, house floor (`Ground_Patio`, `House_Concrete`) | Concrete034, Concrete047A, PavingStones138 |
| Ground (`Ground_Gravel`) | Gravel043, Ground079S |
| Pool coping (`Pool_Coping`) | PavingStones150, Concrete042A |
| Pool shell and tile line (`Pool_Plaster`, `Pool_TileLine`) | Tiles141, Tiles139 (tile line); keep the plaster plain or use Plaster001 tinted |
| Pergola (`House_Pergola`) | Planks037A, WoodFloor043 |

**Fix.**
1. New script `T850/scripts/textures/fetch_backyard_textures.py`: a pinned table
   `{material name: (ambientCG id, "1K-JPG", sha256)}`. Downloads each zip once into a cache that is
   not committed (`T850/build/texture-cache/`), verifies the sha256 (record it on the first download),
   extracts Color, NormalGL, Roughness and AmbientOcclusion. Head comment: source, license (CC0) and
   the IDs. No network access from the engine.
2. `build_backyard.py`: give `material()` an optional `textures=` dict and `tile_m` (metres per
   texture repeat). Build the node graph the glTF exporter understands: Image Texture (Color, sRGB) →
   Base Color; Image Texture (Roughness, Non-Color) → Roughness; Image Texture (NormalGL, Non-Color)
   → Normal Map node → Normal. Keep the existing colour as a tint only if the texture is too bright
   or dark (multiply node) — otherwise drop it. AO is optional; the exporter needs the "glTF Material
   Output" group for it, so do it last.
3. **UVs in metres.** `uv_unwrap()` uses `smart_project`, whose scale is arbitrary per object, so a
   tiling texture would come out a different size on every box. Add `uv_world_box(obj, tile_m)`: per
   face, pick the dominant axis of the world normal and set `uv = (the other two world coordinates)
   / tile_m`. Use it for all textured parts. **Never touch the water mesh's UVs** — they are the
   solver mapping (implementation record 3.2) and the water gets no texture.
4. Export with tangents (`export_tangents=True`) and JPEG images embedded in the `.glb`
   (`export_image_format="JPEG"` or `"AUTO"`). The `.glb` files are generated and never committed,
   so embedding is the simplest delivery. Expect the house GLB to grow from 0.12 MB to a few MB.
5. Rerun the generator (Blender command in implementation record 3.1). The mesh cache under
   `Models/Backyard/.t8cache` keys on file size and write time, so a new GLB invalidates it by itself.

**Acceptance.**
* `git diff T850/Assets/Models/Backyard/backyard_layout.json` is empty: textures must not move
  geometry.
* Log at info: the `[MeshInfo] KeyFlags` lines for the house, wall, ground and pool show
  `baseColorMap=1 normalMap=1 tangents=1`.
* Back-buffer dump at frame 300 before/after from the default camera: brick, plaster, corrugated
  steel and paving visible at a believable scale (a 1 m feature looks 1 m). Check grazing angles on
  the ground for shimmering; if it shimmers, mips are missing — report it, do not hide it with blur.
* Scenes 7, 1 and 6 on all five APIs: 0 errors (GL and WebGPU decode the JPEGs to RGBA8).

**Outcome (Stage 4b) — DONE.** `scripts/textures/fetch_backyard_textures.py` with six pinned
ambientCG sets (sha256 verified), NormalGL only; `material()` gains texture nodes;
`uv_world_box()` gives metric UVs (tile_m per repeat); tangents and embedded JPEGs on export, water
excluded. Measured: layout diff empty; house GLB 0.12 → 5.17 MB; `House_Stucco` exports
`baseColorFactor (1, 0.88, 0.74)` + baseColor + normal + metallicRoughness; 77/128 material
instances textured with `baseColorMap=1 normalMap=1 tangents=1`; frame-300 dump shows all surfaces;
fps 187.5 → 177.2 (−5.5 %) same-session on D3D12; 15/15 scene/API combinations 0 errors; both
self-tests PASS. Limitation: no occlusionTexture (needs the glTF Material Output group). Gotcha:
ambientCG returns 403 without a User-Agent.
* Same-session fps before/after on D3D12 and Vulkan; a few percent is acceptable, report it.
* Pool, water and solver untouched: `--backyard-selftest` PASS with identical numbers.

---

### W-24 — The house roof is upside down (P2, S, suggested: either)

**Problem (seen in the frame-300 back-buffer dump).** The two roof slabs slope the wrong way: the
outer edges sit high and the middle sags, so the roof is a V with the ridge cap floating above the
valley. Cause, in `build_house()` in `T850/scripts/blender/build_backyard.py`: each slab is rotated
about X by `sign * angle`. Rotating by +θ about X raises the slab's +Y end, so the slab on the +Y
half (`sign = +1`, placed at `cy + run/2`) lifts its *outer* edge, and the −Y slab (`sign = −1`)
lifts its outer edge too. Both must lift the edge that touches the ridge instead.

**Fix.** Use `rot=(-sign * angle, 0.0, 0.0)` for `House_Roof_Front` / `House_Roof_Back` **and** for
the standing seams (`House_Seam_Front_*` / `House_Seam_Back_*`), which share the same rotation.
Then:
* After the fix each slab's ridge-side edge is at `h + rise/2 + (slope_len/2)·sin(angle) = ridge` (5.1
  m) and its eave edge at `h` (3.2 m) with a 0.5 m overhang; the ridge cap at `ridge` closes the top.
  Print the min/max world Z of both slabs from the script to prove it.
* The roof now leaves the two gable triangles open at `x = min_x` and `x = max_x` (the walls stop at
  the eave height). Add two triangular gable walls in stucco (a prism from a bmesh with 3+3 verts,
  or a box with its top two verts merged at the ridge) so the roof does not float.
* The chimney passes through the roof on the +Y side; check it still pokes out (its top is at
  ≈ 6.0 m, the roof surface there is ≈ 4.75 m).

**Acceptance.** Back-buffer dump at frame 300 from the default camera shows a pitched roof with the
ridge highest and the eaves resting on the walls; side view (move the camera in the GUI) shows the
gables closed. `git diff backyard_layout.json` is empty (house extents unchanged). Scene 7 0 errors;
self-tests unchanged (the house is not part of the solver). Regenerate only through the script —
no hand edits to the GLB.

**Outcome (Stage 4a) — DONE.** `rot=(-sign*angle, 0, 0)` on the slabs and the seams; two
`from_pydata` gable prisms at `min_x`/`max_x`, flush with the wall face. Generator prints
`world Z 3.152 .. 5.148 m` for both slabs (mid-plane 3.20…5.10); house GLB 78 → 80 meshes;
layout JSON diff empty; frame-300 dump pitched; 0 errors on five APIs; self-tests PASS.

---

### W-25 — A different PBR material on every spawned ball (P2, M) — owner request

**Goal.** Every ball that lands in the pool is visibly a different material, from a curated palette
of **32 CC0 PBR sets** (the owner asked for at least 32). The scripted ball keeps slot 0 of the
palette. Nothing about the solver changes.

**Source and licence.** ambientCG, **CC0 1.0 Universal** (`https://docs.ambientcg.com/license/`):
the raw files may be shipped inside a project and no credit is required. All 51 ids below were
checked against the API (`https://ambientcg.com/api/v2/full_json?id=<ID>&include=displayData`) and
every one of them has a downloadable 1K-JPG archive
(`https://ambientcg.com/get?file=<ID>_1K-JPG.zip`). An archive holds `_Color`, `_NormalGL`,
`_NormalDX`, `_Roughness`, `_Displacement` and often `_AmbientOcclusion`. **`NormalDX` must never be
used** — it inverts every bump. ambientCG has no metallic map: metalness is a property of the asset
family (`Metal*` = 1, marble/wood/stone = 0).

**The 32-material palette.** "Metal" is the constant written into the ORM map's blue channel.

| # | id | What it looks like | Metal |
|---|---|---|---|
| 1 | Metal009 | Brushed silver | 1 |
| 2 | Metal049A | Smooth silver | 1 |
| 3 | Metal048A | Clean gold | 1 |
| 4 | Metal043A | Clean copper | 1 |
| 5 | Metal043B | Dirty copper | 1 |
| 6 | Metal032 | Plain grey metal | 1 |
| 7 | Metal027 | Powder-coated black | 1 |
| 8 | Metal038 | Scratched steel | 1 |
| 9 | Metal046A | Clean dark metal | 1 |
| 10 | Metal046B | Dirty dark metal | 1 |
| 11 | MetalPlates006 | Riveted steel plates | 1 |
| 12 | DiamondPlate001 | Diamond tread plate | 1 |
| 13 | Chainmail001 | Chain mail | 1 |
| 14 | CorrugatedSteel005 | Corrugated steel | 1 |
| 15 | Marble001 | White marble | 0 |
| 16 | Marble002 | Black marble | 0 |
| 17 | Marble003 | Grey marble | 0 |
| 18 | Marble012 | Veined white marble | 0 |
| 19 | Marble016 | Black reflective marble | 0 |
| 20 | Onyx001 | Onyx | 0 |
| 21 | Travertine001 | Travertine | 0 |
| 22 | Terrazzo001 | Terrazzo, fine | 0 |
| 23 | Terrazzo005 | Terrazzo, coarse | 0 |
| 24 | Tiles139 | Ceramic tiles | 0 |
| 25 | Concrete047A | Polished concrete | 0 |
| 26 | Wood001 | Oak planks | 0 |
| 27 | Wood021 | Weathered wood | 0 |
| 28 | Wood035 | Dark wood | 0 |
| 29 | WoodFloor043 | Floor planks | 0 |
| 30 | Rock030 | Rough rock | 0 |
| 31 | Leather008 | Leather | 0 |
| 32 | Cork001 | Cork | 0 |

**19 spare ids**, verified but not shipped in the palette, for when 32 is not enough:
Metal030, MetalPlates007, CorrugatedSteel009, Marble008, Terrazzo003, Terrazzo007, Tiles141,
Wood051, Wood062, Planks021, Concrete034, PavingStones150, Rock029, Bricks075A, Asphalt012,
Fabric019, Rubber001, Plastic001, Porcelain001. Adding one is a line in the manifest plus a fetch,
because the loader is data-driven.

**How the engine actually reads material maps (checked in `Assets/Shaders/FS_Mesh.hlsl`).**

- `METALLIC_MAP` samples a *metallic-roughness* texture: `metallic = PBRParams.x * sample.b` and
  `roughness = PBRParams.y * sample.g`. This is the only path that gives independent roughness, so
  a palette entry is one packed **ORM** map (`R` = AO, `G` = roughness, `B` = metalness) plus a
  base-colour map and a `NormalGL` map.
- `GLOSS_MAP` is `#elif`-ed after `METALLIC_MAP` and reads `.r` with **no** factor multiply — do not
  use it for the palette.
- `OCCLUSION_MAP` is gated on `MaterialParams7.x`, which the renderer fills from the *subset's*
  material (`mat->textures[Occlusion] ? 1 : 0`), so a per-instance AO map is inert. **Balls therefore
  skip AO**; the ORM map still carries AO in `R` (harmless, it is never sampled) so the same file can
  be reused if AO is ever plumbed per instance.
- Base colour is multiplied by `baseColorFactor` in glTF tangent space, so the ball material's factor
  must be white. Because the material is shared by every ball, `metallicFactor` and `roughnessFactor`
  must both be **1.0** — the maps then carry all the variation. A ball with no map falls back to a
  matte-metal look, which is why the fetch step is mandatory.
- Instance UV sets: `MapShareBaseSet(TexCoordSets.z) ? uv : …` makes the metallic map share UV0 when
  the material's `metallicTexCoord` is 0, which is the default. No UV transform is applied.

**Per-instance plumbing.**

- `PrimitiveInst` already carries `Texture* Textures[MaxPrimitiveTextures]`, `ShaderKey gKey` and
  `SetTexture(tex, slot)`; `PrimitiveInst::Draw()` pushes them into the shared `PrimitiveBase`, draws
  and restores them. That is the mechanism — no GPU instancing buffer is involved.
- `RenderMesh::Draw` currently reads the instance slot only for normals (`Textures[3]`). Add
  base-colour (0) and metallic-roughness (6) fallbacks, but **behind a new
  `ShaderKey::INSTANCE_MATERIAL_MAPS` bit** so the scenes that already put an atlas in instance slot 0
  (Minecraft `Meshes[idx].SetTexture(m_atlasTexture, 0)`, VoxelScene) keep today's behaviour.
  Propagate the bit in `finalKey` exactly as `FLUID_SURFACE` is propagated, and merge it in
  `PrimitiveInst::SetGlobalKey` from `PersistentFeatureBits`.
- Slot numbering: 0 base colour, 3 normal, 6 metallic-roughness; 22/23/24 stay the water's. Every
  new instance slot needs a `bindTextureOnce` branch, and `MaxPrimitiveTextures` is already 28.

**Assets.**

- `Backyard_Ball.glb` carries **only `POSITION` and `NORMAL`** — no UVs, no tangents, so no texture
  can bind. `scripts/models/build_ball.py` (pure Python, writes glTF directly) must emit spherical
  `TEXCOORD_0` and `TANGENT`, and the material must become
  `baseColorFactor = [1,1,1,1]`, `metallicFactor = 1.0`, `roughnessFactor = 1.0`.
- `scripts/textures/fetch_ball_materials.py`: same pattern as
  `fetch_backyard_textures.py` (pinned sha256 per archive, a User-Agent or ambientCG answers 403),
  downloads into the uncommitted `build/texture-cache`, then packs runtime files with Pillow
  (10.1.0 is installed) into `Assets/Textures/Ball/<ID>/`:
  `Color.jpg` (sRGB), `Normal.png` (OpenGL convention), `ORM.png` (linear: R = AO or 255, G =
  roughness, B = metalness × 255). Default 512² — 1 MB per map decoded, so the whole palette is
  ~96 MB of texture memory; `--resolution` allows 256/512/1024.
- **Delivery is the cloud texture manifest, not a per-machine ambientCG download.** Only the
  authoring machine fetches from ambientCG; `scripts/PushCloudTextures.ps1` uploads the packed files
  to the `t850-textures` R2 bucket and republishes the shared manifest, and every other machine gets
  them from `scripts/DownloadTextures.ps1` like the sky cubemaps. See
  `documentation/development/cloud-assets.md` ("Publishing Assets to R2").
- `Assets/Textures/Ball/` is **not committed** (`.gitignore` drops the directory). The Backyard
  scene already requires a script run for its GLBs, so this does not change the build story.
- `Assets/Scenes/BackyardBallMaterials.json` is committed: `{"version": 1, "resolution": 512,
  "materials": [{"id": "Metal009", "label": "Brushed silver", "metal": 1.0}, …]}`, 32 entries plus
  19 spare ids. Unknown or malformed ids are a hard error naming the file, the same discipline as
  `BackyardWater.json`.

**Assignment.** Ball with body index *k* (1…N) takes `materials[(k - 1) % 32]`; body 0 (the scripted
ball) takes `materials[0]`. Deterministic, independent of `spawnSeed`, and a permutation for the
first 32 spawns. The spawn log names the material. Textures are created lazily on first use and kept
for the process lifetime (resident count ≤ palette size, independent of the ball count).

**Acceptance.**

1. `BackyardBallMaterials.json` lists ≥ 32 entries and every one resolves to three files that load
   on the running API (gate 10).
2. Gate 10 also asserts the assignment is a permutation for the first 32 bodies, and that each ball
   instance's slots 0/3/6 are non-null, pairwise distinct between two balls, and that
   `INSTANCE_MATERIAL_MAPS` | `DIFFUSE_MAP` | `NORMAL_MAP` | `METALLIC_MAP` are set in its key.
3. Frame probe: `--backyard-spawn 32` with a fixed `spawnSeed`, one dump frame; project each body's
   centre from `snapshot.json`'s `camVP` into the back buffer and assert the 32 sampled colours are
   pairwise distinct (documented threshold) and that a metal ball and a marble ball differ in mean
   lightness. Reported on D3D12 and Vulkan.
4. Gates 1–9 unchanged; both self-tests PASS on D3D12 and Vulkan; scenes 0/1/6 and 7 on five APIs
   with 0 `[ERROR]` lines; same-session fps for 0 / 7 / 32 balls.
5. Regenerate only `Backyard_Ball.glb`; the other Backyard GLBs and `backyard_layout.json` must not
   change.

**Limitations to record.** No per-instance AO (subset-gated), no per-ball tint or roughness scale
(the factors are shared; a texture-level variation would be needed), palette bounded at 32 resident
sets, and texture files are not committed so a fresh clone must run `DownloadTextures.ps1`.

**Outcome (2026-10-30) — done: the asset half by the local agent, the engine half by the reviewer.**

- `scripts/textures/fetch_ball_materials.py` packs all 32 sets at 512² (96 files, 16.78 MB) from the
  pinned ambientCG archives; a corrupted archive aborts with a sha256 mismatch (negative-tested).
- `scripts/PushCloudTextures.ps1` (new) uploaded the 96 objects to `t850-textures` under
  `Textures/Ball/` and republished `manifest.json` to **both** buckets. The manifest went from 114
  entries / 42 textures to **210 entries / 138 textures**, `manifestVersion` 1, 110,459 bytes; each
  new entry carries a `sha256`.
- Verified: every upload read back from the API matched the local file; both buckets still hold the
  same manifest; the public `r2.dev` manifest and a sample of ball objects serve the new content;
  a second run reports "the manifest is already current" and publishes nothing.
- `DownloadTextures.ps1` reports 138/138 ready, and deleting `Ball/Marble016/ORM.png` made it
  re-download the file and verify its sha256.
- **Engine half done in `e055efec` (reviewer).** It differs from the plan above in three ways.
  - The ball is a spherified cube with UVs, so no pole faces the camera.
  - The GLB declares the three maps as 1×1 placeholders, so the textured permutation and the
    MikkTSpace tangents come from the glTF loader.
  - The override is a plain `PrimitiveInst::InstanceMaterialMaps` flag, not a `ShaderKey` bit, and
    the owner asked for random picks, so a seeded shuffle bag on its own RNG replaces
    `(k - 1) % 32`.

  Verified: 32 distinct materials across 32 bodies, all 32 used by 100 balls on D3D12, Vulkan and
  D3D11 with 0 errors, gate 9 unchanged, scenes 7/1/6 on five APIs 0 errors, and 100-ball fps within
  noise of the placeholder look. Details are in the staged plan, section 4, "Stage 4d, part 2".
- **Proof closed in this session (gate 10 + probe).** Gate 10 runs headless with every
  `--backyard-selftest`: palette >= 32, every set on disk, every set decodes on the running API, and
  the real shuffle bag is a permutation (`palette=32 onDisk=32 decoded=32 bagPermutation=ok` on
  D3D12 and Vulkan; a missing set fails with exit 1 and names it).
  `scripts/validation/probe_ball_materials.py` is the pixel half: same seed, one run with the palette
  and one with the palette file moved aside, each ball projected with the engine's `camVP` and its
  patch required to change (mean difference 76.6 / 76.7 on D3D12 / Vulkan). `--backyard-spawn-seed`
  was added so the layout is reproducible. Details in the staged plan, "Stage 4d, part 3".

---

### W-26 — The surface kernel compensates for trapped air (P2, S) — debt from W-02

**Problem.** `CS_FluidSurface3D` computes a column's elevation from `water`, the integral of α in
that column. With exact (R32F) α the air a plunging body entrains stays beside the ball — the solver
has no bubble buoyancy (W-07) — so the integral reads as missing water and the drawn surface dipped
in a 1.7–3 cm moat around a floating ball. `3c4e33c0` made it look right by counting air deeper than
`TRAPPED_AIR_DEPTH_CELLS = 2.0` as volume:

```
trapped   = Σ over open rows k of saturate(water − (k + 0.5) − 2) · (1 − α_k)
elevation = BasinMinY + (water + trapped) · CellSize
```

That is a **renderer-side compensation for a solver gap**: it hides entrained air rather than moving
it, so it cannot stay forever. Two concrete risks: (a) once W-07 displaces water instead of deleting
it, or a bubble mechanism exists, the term double-counts and the surface reads high; (b) it is a
`Height`-deep loop per column, so it scales with pool depth.

**Fix.** Remove the term and re-run gate 7 (float ring−far and covered min−ring). If the moat returns,
it means the transport is still leaving air where it should not, and the correct response is W-03 /
W-07, not a larger ramp. Record the measured before/after in the implementation record §7 either way.

**Acceptance.** Gate 7 within its limits **without** the term, on D3D12 and Vulkan, with the
surface-texture gate and the full backyard self-test unchanged; the frame-dump check that the water
around a floating ball is level.

**Attempt 1 (2026-10-31) — measured, term kept.** `TRAPPED_AIR_DEPTH_CELLS` was set to 1000 (term
identically zero) and the self-test run on both APIs:

| | with the term | without it |
|---|---|---|
| `ring−far` D3D12 / Vulkan | −2.0 / −1.4 mm | **−7.4 / −6.7 mm** |
| `covered min−ring` D3D12 / Vulkan | −15.9 / −13.8 mm | **−20.7** / −19.1 mm (limit 20) |
| band profile vs far (cm) | −0.9 −1.0 −0.8 −0.2 | −1.8 −2.0 −1.7 −0.7 |
| float flicker | 0.0005 m | 0.0006 m |
| `submerged`, ball `y` | 0.60, −0.073 | 0.60, −0.073 |

So the term is worth **4.8 mm** of moat and D3D12 then exceeds gate 7 by 0.7 mm; Vulkan scrapes in
at 19.1. Two conclusions: (a) the van Leer flux did **not** reduce the moat — it sharpened the
far-field interface, but the moat is water the *body deletes*, which is W-07 by definition; (b) a
gate decided by ±0.8 mm between two APIs, on a term worth 4.8 mm, is not something to relax. The
term therefore stays until W-07 gives the body sub-cell coverage, and this table is the acceptance
target for that stage. The flicker and the buoyancy are unaffected, so the risk named in (a) above
(air hidden, double counting later) is the only live one.

---

### W-27 — The wind tunable does nothing (P3, S) — found 2026-10-31

**Problem.** `windX` / `windY` / `windZ` are JSON tunables, sliders, and a forcing term in
`CS_FluidForces3D`, but they are applied only to the top rows of the domain:

```hlsl
const float surfaceY = SurfaceLevel * float(Height);
if (float(id.y) > surfaceY - 2.0f) { /* u += WindX * gain; ... */ }
```

`SurfaceLevel` (0.96 from `BackyardWater.json`) is a fraction of the **domain height**, not of the
water depth. The pool's domain spans Y −1.60…+0.38 m but the water only fills 0.788 of it, so the
waterline sits at row ≈ 17 of 22 while `SurfaceLevel · Height` is 21.1: the wind only ever touches
rows 20–21, which are **air**. Measured: raising `windX` from 0.6 to 5.0 (8×) left a scene-7 run
bit-identical — same ball path, same sampled flow, to three decimals.

**Why it matters now.** W-06 step 3 (the body feels the current) had to be verified with a forced
flow in the integrator instead of with a real wind-driven current, because there is none.

**Fix.** Drive the `SurfaceLevel` fraction off `basinMaxY − basinMinY` against the fill level the
solver actually seeds (the layout's `waterPlaneY`), or pass the waterline row in the constants
instead of deriving it from a fraction. Then re-check that a 2× wind moves the ball, and that the
still-pool gate is unaffected at the default wind.

**Acceptance.** A scene-7 run with `windX` 0 vs 2 differs measurably (`flow` in
`--backyard-ball-probe`), still-pool gate unchanged, no new self-test failures.

---

## 6. Hand-back checklist

* [ ] Only the files the task names were changed (plus registry entries the task requires).
* [ ] R1–R9 checked; every new buffer/texture read is guarded (R1).
* [ ] x64 Release builds with no new warnings.
* [ ] Fluid self-test and `--backyard-selftest` (both with `--scene 7`) PASS on D3D12 and Vulkan.
* [ ] Every new source file is in `DayScene.vcxproj`, `DayScene/CMakeLists.txt` **and**
  `cmake/AndroidBuild.cmake` (the registration validator does not check Backyard files).
* [ ] Scene 7 run: 0 `[ERROR]` lines; the section 2.4 metrics reported before and after.
* [ ] Visual check (4.4) still shows circular rings.
* [ ] `T850/config.json` and generated assets not committed.
