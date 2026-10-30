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
| W-11 items 1, 2, 4: JSON tunables, three solver values, comment | **Done** (Stage 4) | — | `Scenes/BackyardWater.json` drives 101 settable keys; strict key validation; `alphaCompression`, `maxSubstepDt` and the volume-check interval are tunables with GUI sliders; see Stage 4 in the staged plan |
| W-15 Solver and frame cost | Measured; 3 fixes done (review) | `3a371903` | D3D12 191 → 219 fps, Vulkan 298 → 313 fps at 720p; P1–P9 open |
| W-09 Water optics: depth, refraction, Fresnel | Done | `517ae6bf` | Accepted. Floor texture logs depth 0.300…1.560 m; `MaxPrimitiveTextures` 24 → 28 is safe (bones/lightmap bind outside the array) |
| W-10 Water invisible to depth post effects | Done, **redesigned in review** | `96d84638` + `47500166` | The extra pass duplicated `Deferred:DEPTH`; removed. CoC now uses the nearer depth; see W-10 |
| W-21 Vulkan fluid self-test fails inside scene 7 | **Done** (Stage 3c) | — | Root cause was uninitialised volume memory, not Vulkan frame state; `TryInitialize` now clears; 6/6 PASS scene 7 Vulkan |
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
W-21. **Next: W-21 (Stage 3c, small), then Stage 4 (W-11).** Step-by-step brief: section 0 of
[backyard-water-staged-plan.md](backyard-water-staged-plan.md).

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
.\DayScene.exe --scene 0 --api d3d12  --fluid-selftest --logLevel info --logFile selftest_d3d12.log
.\DayScene.exe --scene 0 --api vulkan --fluid-selftest --logLevel info --logFile selftest_vk.log
```

Look for `[FluidSelfTest] PASS`. Report the `projection` and `massConservation` lines.

`--fluid-selftest` checks the solver in isolation on its own 32×16×16 grid, so it runs under scene
0 (this is the one exception to "always pass `--scene 7`"). Under scene 7 it fails on Vulkan after
the Backyard scene initialises — that is W-21, not a regression of your change.

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

---

### W-05 — Moving the ball re-uploads the wall mask synchronously (P2, M, suggested: DeepSeek)

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

### W-15 — Solver and frame cost (P1, M–L) — measured in review; three fixes done, P1–P9 open

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
| P1 | Volume correction is a synchronous readback of the whole α volume as RGBA (16 B/voxel, 4× what is needed) plus a CPU sum, every 15 frames. D3D12 `ReadComputeBuffer` also creates a new committed readback resource per call and calls `WaitForGPU` | `MeasureVolumeFraction`, `D3D12Driver::ReadComputeBuffer` | GPU reduction (α total, interface α, headroom) into a 3-float buffer; read the previous frame's result through a persistent readback buffer and a fence (W-04 step 5) | ≈ 1.0 ms D3D12, 0.7 ms Vulkan |
| P2 | Pressure: 60 red-black iterations = 120 full-grid dispatches per substep. Half the threads only copy the other colour through (write-only storage + ping-pong), and the scalar lives in RGBA16F | `CS_FluidPressure3D`, `StepInternal` | After W-02 (R32F): in-place red-black on `RWTexture3D<float>` (typed UAV loads of R32F are universal), dispatching only the active colour. Then fewer iterations with a logged residual, several sweeps per dispatch in groupshared tiles, or MGPCG | up to ≈ 1.2–1.8 ms D3D12 |
| P3 | D3D12 barriers: every dispatch issues a transition SRV→UAV, a UAV barrier, then a transition UAV→SRV, each in its own `ResourceBarrier` call. The transition back is unnecessary for compute-only 3D volumes, and the UAV barrier is redundant when a transition follows | `D3D12Driver::DispatchCompute` | Leave volumes in their tracked state; batch pending transitions into one call at the next dispatch; UAV barriers only for UAV→UAV reuse | GPU idle between the ≈ 130 dispatches; engine-wide, test every compute user |
| P4 | Vulkan records ≈ 10 µs per dispatch (1.3 ms per frame) | Vulkan compute dispatch (descriptor set allocation and update per dispatch) | Cache descriptor sets per pipeline + binding set; the solver alternates between two ping-pong states, so a small cache almost always hits | ≈ 1 ms CPU on Vulkan |
| P5 | D3D12 CPU per dispatch: six `unordered_set`s and a vector allocated, `dynamic_cast` per binding, keep-alive map insert per resource, `SetDescriptorHeaps`/root signature/PSO rebound even when unchanged (the pressure PSO 120× in a row) | `D3D12Driver::DispatchCompute` | Validate once per pipeline and layout, small fixed arrays, skip redundant state | 0.37 ms today; matters once the GPU side is fixed |
| P6 | The moving body re-creates the whole wall-mask buffer (copy of 110k words, new GPU buffer, upload) whenever its cell footprint changes: every substep while it falls or bobs | `UpdateWallMaskForBody` | W-05 (analytic body in the kernels) removes it; interim: one persistent buffer updated in place | allocation churn and spikes during the drop |
| P7 | The surface kernel no longer reads its previous output, so the surface ping-pong pair and the copy dispatch are unnecessary | `StepInternal`, `PublishSurface` | Write straight into the output texture (it already has the storage flag) | one dispatch and barrier per frame |
| P8 | The render graph carries template passes the backyard never uses: Torch Particles (a full-resolution RGBA16F target cleared and filled by compute every frame), Light Add (adds that empty target) and Debug Cascade Regions (a debug fullscreen pass drawn every frame) | `BackyardScene_RenderGraph.json` | Remove Torch; make Light Add a plain copy or rewire the HDR chain; enable the debug pass only when toggled | ≈ 0.03 ms at 720p, ×4 at 1440p; 7–29 MB |
| P9 | Readbacks go texture → compute copy → structured buffer → copy → readback heap | `ReadVolumeFraction`, `ReadVelocity`, `ReadSurface` | A driver call that copies a texture region straight to a readback buffer | debug paths; low |

Checked and fine: the whole solver runs in compute shaders (no pixel-shader simulation); the
post-processing passes that have compute versions already use them; the 3D volumes need both UAV
and SRV access, which is what they have; GBuffer, shadows and the water draw are cheap at 720p.

**Profiling pitfalls.** The engine profiler records scopes only while its frame is open, and
`FluidSimulation::Step` runs in `OnUpdate` before that, so GPU scopes placed inside the solver are
silently dropped. Use frame-rate A/B with `perf.ps1`, or move the profiler's frame start before
`OnUpdate`. In review the Vulkan `--profile --profileFrames 1200` run never exited (killed at
300 s); D3D12 exits normally.

**Acceptance (per item).** Before/after table from `perf.ps1` (median of the four windows) on
D3D12 and Vulkan; `--backyard-selftest` and `--fluid-selftest` PASS on both; the scene metrics in
2.4 unchanged.

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

## 6. Hand-back checklist

* [ ] Only the files the task names were changed (plus registry entries the task requires).
* [ ] R1–R9 checked; every new buffer/texture read is guarded (R1).
* [ ] x64 Release builds with no new warnings.
* [ ] Fluid self-test (`--scene 0`, see 4.2) and `--backyard-selftest` (`--scene 7`) PASS on D3D12 and Vulkan.
* [ ] Scene 7 run: 0 `[ERROR]` lines; the section 2.4 metrics reported before and after.
* [ ] Visual check (4.4) still shows circular rings.
* [ ] `T850/config.json` and generated assets not committed.
