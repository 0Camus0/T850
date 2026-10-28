# Backyard Water — Staged Plan

Companion to [backyard-water-remaining-work.md](backyard-water-remaining-work.md) (the task
catalogue W-01…W-17) and [backyard-water-simulation.md](backyard-water-simulation.md) (the
implementation record).

The task catalogue is the source of truth for **what** each task is and how to prove it. This
document is only about **order and session boundaries**, so each stage can be executed cold, in
one context, without carrying the whole backlog.

Baseline for all work: `01ccbd82` (HEAD), after `e90752b4` rebuilt the solver on a staggered
grid.

---

## 1. Rules for running a stage

1. **One stage per session.** Do not start the next stage in the same context.
2. **Read first:** section 2 (current architecture) and section 3 (rules R1–R9) of
   `backyard-water-remaining-work.md`. Most failures came from breaking an R rule, not the
   physics.
3. **Record the baseline before changing anything.** Stage 0 produces the numbers every later
   stage compares against.
4. **Report measured numbers, not "pass".** Every stage has an acceptance list; quote the
   numbers.
5. **Never combine.** No drive-by refactors, no reformatting, no extra kernels.
6. **Append results** to section 4 of this document under the stage heading before closing the
   session — that is how the next session starts informed.

---

## 2. Stage map

| Stage | Tasks | Size | Why here |
|---|---|---|---|
| 0. Baseline & guardrails | — | S | Without a recorded baseline no later claim is checkable |
| 1a. Vulkan gate must be trustworthy | W-18 | S | **DONE** — Vulkan self-test now PASS |
| 1b. Make failures provable | W-12 | M | **DONE** — gates 1, 2, 3, 6 pass on both APIs |
| 2. Cheap visible defects | W-01, W-08, W-13 | S–M | High visible payoff, low risk, no overlap with the solver |
| 3. Rendering correctness | W-09, W-10 | M | Independent of the solver; improves what already works |
| 4. Data-driven tunables | W-11 | M | Unlocks tuning without rebuilds; needed before solver tuning |
| 5a. 32-bit storage | W-02 | M | Prerequisite for the transport work |
| 5b. Conservative transport | W-03 | L | The remaining source of mass loss |
| 6a. Analytic body + cheap readback | W-05, W-04 | M | Removes per-substep uploads; unblocks 6b |
| 6b. Displacement + ball feels water | W-07, W-06 | M | Needs 6a's previous/current body state and async readback |
| 7. Solver cost | W-15 | M | Measure before optimising |
| 8. Cleanup, docs, platforms | W-14, W-16, W-17 | S–M | Only once the code is stable |

### Additions found during Stage 0

- **W-18 — Vulkan fluid self-test exhausts the descriptor pool** (P1, S). Fails at step 109 of a
  400-frame run with `VK_ERROR_OUT_OF_POOL_MEMORY`; every physics gate passes identically on
  Vulkan first. The scene itself is clean on Vulkan, so this is harness plumbing, not the solver.
  Until it is fixed the Vulkan gate is untrustworthy, which undermines every stage that relies on
  "self-test PASS on D3D12 and Vulkan". **Must land in Stage 1** alongside W-12, before any
  solver change, or we will not be able to tell a real Vulkan regression from this defect.

### Dependency graph

```
0 ──► 1 ──► 2 ──► 3 ──► 4 ──► 5a ──► 5b ──► 6a ──► 6b ──► 7 ──► 8
                     │                         │      │
                     └─ independent of solver  └──────┘
```

Hard edges:

- **W-02 before W-03** — the transport work should land on 32-bit α, otherwise half-float
  rounding masks whether the scheme actually conserves.
- **W-04 before W-06** — the ball needs the local surface level, which needs a cheap readback.
- **W-05 before W-07** — displacement needs the body's previous and current configuration, which
  is far cleaner once the body is analytic.
- **W-11 before any solver tuning** — otherwise every change needs a rebuild to test.
- **W-14 after W-05/W-07** — those tasks add `ObjectConstants` to more kernels; deduplicating
  first would mean doing it twice.
- **W-16 last** — the record must describe the final code.

---

## 3. Stage details

Each stage lists: scope, files likely touched, acceptance, and exit criteria. Task text lives in
the catalogue; it is not repeated here.

### Measurement protocol (from Stage 0)

Two modes, because the two families of metric need different things:

- **Mode A — deterministic fixed step** (`fixedTimeStep = true`, `dt = 1/120 s`): all physics A/B
  numbers. Without it two runs land on different physical states at the same wall-clock time and
  cross-stage comparison is meaningless. D3D12 vs Vulkan at rest already differ under real dt
  (rms 0.0205 vs 0.0279), which proves the point.
- **Mode B — real dt** (clamped, CFL substeps): integration sanity, substep/CFL distribution and
  performance. This is the mode that exercises the clamp logic, so it must not be replaced by
  Mode A — it exercises something A cannot.

Both modes are needed; neither is a substitute for the other. `fixedTimeStep` exists as a tunable
but is not CLI-exposed, which is part of Stage 1.

Sampling rules:
- anchor metrics to **sim time**, not frame index or wall clock (`[sphere] t=…` is sim time);
- prefer anchoring to **physical events** over fixed times where possible ("ball velocity < 0.01
  for 10 consecutive frames" rather than "t = 10.7 s");
- run 3 repeats and record the **median** for real-dt metrics; one repeat suffices in fixed-step
  if two runs agree;
- report **p50 and p99**, not just means, for anything that can spike;
- exclude the ball region (1.5 cells around the body surface) from surface-rms so the −35 cm
  hollow cannot mask solver flatness regressions.

### Stage 0 — Baseline & guardrails

**Do**
- Build `T850.sln` / Release / x64 (`/m:1` if `-m:4` ICEs).
- Run `--fluid-selftest` on D3D12 and Vulkan.
- Run scene 7 at 1280×720 for ~20 s with `--logLevel debug`, capture the `[Backyard]` log lines.
- Fill in the §2.4 table of the catalogue with today's numbers.

**Acceptance** — 0 `[ERROR]` lines; self-test PASS on both APIs; the nine §2.4 metrics recorded.

**Exit** — baseline committed into section 4 of this file. No code changed.

---

### Stage 1a — Vulkan gate must be trustworthy (W-18) — **DONE**

**Problem** — the 400-frame fluid self-test failed at step 109 on Vulkan with
`VK_ERROR_OUT_OF_POOL_MEMORY`. Every physics gate passed identically first, and the Vulkan
*scene* was clean, so it was pool plumbing rather than the solver — but it made the Vulkan gate
unusable.

**Root cause** — `VulkanDriver::BeginFrame` early-returns when `m_frameStarted` is already true
(`if (m_frameStarted) return;`). A headless dispatch loop never reaches `CompleteFrame`, so the
frame stays open after the first dispatch and the descriptor pool is reset **exactly once**.
Every later dispatch then accumulates into that one pool until it is exhausted. The interactive
scene never sees this because it resets the pool at every render frame.

**Fix** — `VulkanDriver::RecycleOverflowingComputeFrame()`, called from the compute dispatch path
before `BeginFrame`. It only acts on a frame the *compute path itself* opened
(`m_frameOpenedByCompute`), so a renderer-owned frame can never be recycled under its draws. When
the set count for the current pool generation reaches **6144 (75% of `maxSets` 8192)** it
completes the frame (`SubmitNoPresent`), calls `WaitForGPU()` — required, because resetting a pool
invalidates every set allocated from it and none may still be in flight — and lets the next
`BeginFrame` start a clean generation.

The budget is deliberately a fraction of `maxSets` rather than a small batch size: one
interactive frame uses a few hundred sets and can never reach 6144, so the renderer path is
untouched and no per-frame submit is introduced. A headless loop keeps allocating until it does
reach it, which is exactly the case that used to run the pool dry.

**Files** — `Framework/include/video/vulkan/VulkanDriver.h`,
`Framework/src/video/vulkan/VulkanDriver.cpp`, `Framework/src/video/vulkan/VulkanCompute.cpp`.

**Measured**

| | Before | After |
|---|---|---|
| Vulkan fluid self-test, 400 frames | `Step 109 failed`, `FAIL: failures=1` | **`PASS`**, `meanSpeed=0.1341` |
| Recycles needed | — | 3 (at 6144 sets each) |
| Vulkan scene 7, 12 s | 0 errors | **0 errors**, ball at rest `submerged=0.60`, rms 3.24 cm |
| D3D12 scene 7, 12 s | rms 2.19 cm | **rms 2.19 cm** — unchanged |
| D3D12 self-test | PASS | **PASS** |

Cross-backend agreement after the fix: D3D12 `meanSpeed=0.1340`, Vulkan `0.1341`.

**Environment note** — after editing `VulkanDriver.h` an incremental link produced the
`_EXCEPTION_DISPOSITION` / `std::align_val_t` `C2011` redefinition pair from stale CRT
precompiled state; no `.pch` existed to delete. `MSBuild /t:Rebuild /m:1` clears it. Expect this
after header edits in this project; go straight to a rebuild rather than debugging the headers.

**Exit** — Vulkan gate trustworthy. Every later stage can now rely on "self-test PASS on D3D12
and Vulkan".

---

### Stage 1b — W-12 gates — **DONE (gates 1, 2, 3, 6)**

New `DayScene/BackyardSelfTest.{h,cpp}`, a standalone solver on the real pool grid with no
renderer, plus `--backyard-selftest`, `--backyard-selftest-frames`, `--backyard-no-ball` and
`--fluid-fixed-dt <s>`.

**Gates implemented**

| Gate | Measures | Result |
|---|---|---|
| Still pool | surface bias, rms, max column deviation, mean speed, water lost over 2 s | PASS |
| Isotropy | quadrant annulus medians: **mirror** (X+ vs X−, Z+ vs Z−) and **axis** (X vs Z) | PASS |
| Mass | endpoint drift and worst instantaneous drift over 10 s with the body in the water | PASS |
| Wave propagation | sustained front arrival at r = 0.5 m | PASS |

**Measured (both APIs)**

| Metric | D3D12 | Vulkan | Gate |
|---|---|---|---|
| Still-pool mean | −10.5 mm | −10.5 mm | ≤ 12 mm band |
| Still-pool rms | 0.1 mm | 0.1 mm | ≤ 5 mm |
| Still-pool mean speed | 2.4 mm/s | 2.4 mm/s | ≤ 10 mm/s |
| Water lost in 2 s | 0.693 % | 0.694 % | ≤ 0.80 % |
| Isotropy mirror | 0.65 mm | 0.01 mm | ≤ 3 mm |
| Isotropy axis | 10.3 mm | 10.4 mm | ≤ 16 mm early / 12 mm late |
| Impact time | 0.39 s | 0.39 s | — |
| Arrival at 0.5 m | +0.04 s | +0.04 s | 0.01–0.45 s |
| Mass endpoint drift | +0.007 % | +0.006 % | ≤ 0.05 % |
| Mass worst drift | +0.052 % | +0.054 % | ≤ 0.75 % |

Cross-backend agreement is within noise on every gate — which is the property that was missing
before, because the Vulkan gate was broken.

**Findings**

1. **The correction deficit is not constant.** The catalogue says "~300 cells every 15 frames,
   constant from t = 0". Measured at rest with **no body**: 202 cells at t = 0.05 s rising
   monotonically to 749 at t = 2.0 s, while the correction repays only ~174 cells per check. The
   pool therefore sinks at a steady **1.1 cm/s** (mean −1.5 mm at 0.2 s to −10.5 mm at 2.0 s,
   0.69 % of the water lost in 2 s). This is W-03 confirmed quantitatively, and it is why the
   still-pool bias gate is a *regression band* rather than the strict "must not sink" gate: a
   threshold tight enough to catch drift would fail today's branch. Stage 5b tightens it.
2. **X-vs-Z anisotropy is a property of the transport, not a bug.** Mirror symmetry is excellent
   (0.65 mm) but X and Z amplitudes differ by **10.3 mm** at r = 0.5 m, 0.3 s after impact. The
   alpha transport is operator-split and sweeps one axis at a time, so this is expected. The axis
   band exists to catch a regression and should shrink once the conservative scheme lands.
3. **Arrival is much faster than shallow-water speed.** 0.04 s for 0.5 m implies ~12 m/s, not
   √(g·h) = 3.9 m/s. The near field right after impact is cavity collapse, not a gravity wave, so
   the lower bound only guards a false positive; the meaningful direction is a **stalled** front.
4. **Gate 2 as implemented cannot catch the `id.y` surface-kernel bug**, because it reads the alpha
   volume rather than the surface texture. That bug class needs gate 5 (surface-texture readback),
   which is Stage 1c — this is the concrete reason 1c exists rather than being optional.

**Two bugs found in the harness itself, both now guarded**

- The harness was constructing `FluidSettings` from its own defaults, which still carry
  `forcingAmplitude = 1.0` and `damping = 0.6`. A "still" pool therefore grew from 1.9 mm to
  34.6 mm of surface noise in two seconds with no body and no user forcing — the gate was
  measuring the synthetic sin/cos stir. Settings are now built from `WaterTunables`, exactly as the
  scene does. **This is also the demonstrated proof that a gate can fail:** with the synthetic stir
  present the still-pool gate fails on rms (21.9 → 34.6 mm) and mean speed (67 mm/s).
- The surface residual scales strongly with dt (rms 21.9 mm at 1/60, 19.3 at 1/120, 10.4 at
  1/200, 3.6 at 1/400), so the gate is pinned to **1/300 fixed** to measure surface shape rather
  than the solver's O(dt) residual.

**Exit** — gates 1, 2, 3 and 6 pass on both APIs and are recorded. **Stage 1c** adds gate 5
(surface-texture readback) and gate 4 (velocity at a moving boundary), which are the two that need
new readback APIs.

---

### Stage 1c — Remaining gates (W-12 gates 4 and 5)

**Scope** — the two gates that need new readback APIs, deliberately split out of 1b:

- **Gate 5 (surface-texture readback).** Read the actual surface texture at several positions
  rather than texel (0,0). This is the gate that would catch the **`id.y`/`id.z` dispatch-axis bug**
  class: gate 2 as implemented reads the alpha volume, so a surface kernel that copies the
  near-wall row into every row passes it. Add a 2D texture→buffer copy kernel (the 2D analogue of
  `CS_FluidCopy3D`) plus a `FluidSimulation::ReadSurface` accessor, then assert the surface texture
  varies in **both** grid axes and matches the alpha-derived elevation within one cell.
- **Gate 4 (velocity at a moving boundary).** Ball moving at constant velocity through still water:
  liquid faces next to it carry its normal velocity, and no liquid face points into it relative to
  its velocity. Needs `FluidSimulation::ReadVelocity`. The staggered layout makes this precise:
  texel `(i,j,k).x` is the +X face of cell `(i,j,k)`.
- **α boundedness violations** (count of cells outside [0,1]) — cheap, and the direct target of the
  Stage 5b transport change. Add it here so 5b has its gate ready.

**Files** — new `CS_FluidSurfaceToBuffer.hlsl`, `compute_kernels.json`, `FluidSimulation.{h,cpp}`,
`DayScene/BackyardSelfTest.cpp`.

**Acceptance** — gates 4 and 5 pass on both APIs; the surface-texture gate **fails** when the
`id.y` fix in `CS_FluidSurface3D` is temporarily reverted to `id.z`, then passes again when
restored. That is the specific proof Stage 0 identified as missing.

**Exit** — all six gates in place, and at least one demonstrated to fail on the exact bug class
that survived the previous "verified" pass.

---

### Stage 2 — Cheap visible defects (W-01, W-08, W-13)

**Scope**
- **W-01** — only set `FLUID_SURFACE` on the water instance once `TryInitialize` succeeded, and
  clear it on teardown. Removes the per-frame `GetShader miss` flood on GL/WebGPU.
- **W-08** — treat `j ≥ Height` as AIR in pressure/gradient/extrapolate instead of SOLID.
- **W-13** — delete `CS_FluidSphere3D.hlsl`, `InjectSphere`, `m_removedBuffer`,
  `BuildSpreadConstants`, the `fluid_sphere` registry entry and its pipeline creation, plus the
  unused `FluidSettings` fields. **Note:** that kernel carries the same `id.z` row bug the surface
  kernel had; deleting removes a latent trap.

**Files** — `BackyardScene.cpp`, `CS_FluidPressure3D.hlsl`, `CS_FluidGradient3D.hlsl`,
`CS_FluidExtrapolate3D.hlsl`, `FluidSimulation.{h,cpp}`, `compute_kernels.json`.

**Acceptance** — `--api gl` and `--api webgpu` scene 7: 0 `[ERROR]` lines, water plane visible in
the dump. Heavy drop (density 3.0, height 2.5 m): no α accumulation in the top row. Build,
self-test and D3D12/Vulkan scene unchanged (Stage 0 numbers).

**Exit** — three independent small wins, none touching the numerical scheme.

---

### Stage 3 — Rendering correctness (W-09, W-10)

**Scope**
- Per-column static floor height texture (CPU-built from the static mask) so absorption uses the
  real depth over the steps instead of a constant 1.56 m.
- Depth test the refracted `SceneColorTex` lookup against `SceneDepthTex` so the ball and coping
  stop bleeding into the water.
- Blend the Fresnel normal toward the flat normal so crests change reflectivity without dark
  patches.
- W-10: a depth-only draw of the displaced water gives post effects a surface to see.

**Files** — `FS_Mesh.hlsl`, `RenderMesh.cpp` (new texture slot — remember the per-slot `if`
branch, bug 18 in the record), `BackyardScene.cpp`, render graph JSON if a new target is needed.

**Acceptance** — A/B back-buffer dumps from one snapshot at t = 2.0 s: steps visibly lighter than
the deep end; no ball colour in the water; depth target contains the water surface. 0 errors on
D3D12/Vulkan/D3D11. Frame-time cost of the extra draw reported.

**Exit** — water optics match the geometry; no new passes disabled.

---

### Stage 4 — Data-driven tunables (W-11)

**Scope**
- Derive `gridWidth`/`gridDepth` from the layout the way the height already is, so `cellSize` is
  the only free grid parameter and a change rebuilds automatically.
- Add `alphaCompression`, `maxSubstepDt` and the volume-check interval to `WaterTunables` + GUI.
- Load `WaterTunables` from a `"water"` object in `BackyardScene.json` with glaze
  (`error_on_unknown_keys = true`), falling back to the C++ defaults.
- Fix the stale "Fifteen float4 groups" comment (it is 24).

**Files** — `WaterTunables.h`, `BackyardScene.cpp`, `BackyardScene.json`,
`FluidSimulation.cpp` (layout parsing).

**Acceptance** — editing one JSON value changes the run (show the example). Setting `cellSize` to
0.06 rebuilds 150×33×75 automatically and the still surface stays within ±3 mm of the authored
plane.

**Exit** — the "nothing hardcoded" requirement is measurably satisfied for the water; every later
stage can tune without a rebuild.

---

### Stage 5a — 32-bit storage for α, pressure, divergence (W-02)

**Scope** — add `R32Float` to `ComputeStorageFormat`, the registry parser, the reflection map,
and every backend that maps or validates formats. `CreateVolume` takes a format; α, pressure and
divergence become R32F; velocity stays RGBA16F. Kernels that write them switch to
`RWTexture3D<float>` with `[[spv::format_r32f]]`; readers to `Texture3D<float>`. The clear kernel
needs an R32F permutation (it clears both kinds). Extend `ComputeSelfTest` with an R32F round
trip.

**Files** — `BaseDriver.h`, `ComputeKernelRegistry.cpp`, `ComputeReflection.cpp`,
`D3D11/D3D12/Vulkan/GLCompute.cpp`, `WebGPUDriver.cpp`, `RenderGraph.cpp`, `FluidSimulation.cpp`,
`compute_kernels.json`, all α/pressure/divergence kernels, `ComputeSelfTest.cpp`.

**Acceptance** — compute self-test passes on D3D11/D3D12/Vulkan; fluid self-test passes. Report
the `VolumeCorrection` deficit before/after and surface rms at t ≈ 11 s.

**Exit** — 32-bit fields live on all compute backends. **Do not touch the transport scheme in
this stage.**

---

### Stage 5b — Conservative, bounded transport (W-03)

**Scope** — replace the upwind + `saturate` clip with either the Weymouth–Yue operator-split
dilation scheme (preferred) or a Zalesak-style flux limiter on the existing unsplit form. Keep
the global correction as a safety net and log its deficit; it should approach zero.

**Files** — `CS_FluidAlphaAdvect3D.hlsl` (+ possibly new per-axis kernels),
`compute_kernels.json`, `FluidSimulation.cpp`.

**Acceptance** — deficit < 20 cells per 15 frames over 3000 frames, at rest **and** after impact.
Self-test mass drift within ±0.05%. Surface rms at t ≈ 11 s ≤ 2.5 cm. Rings still render. Tighten
gate 3 of W-12 to the new threshold.

**Exit** — mass loss is solved at the source, not patched by the correction.

---

### Stage 6a — Analytic body and cheap readback (W-05, W-04)

**Scope**
- **W-05** — the body becomes analytic in every kernel (`IsSolid` = static mask **or**
  `IsBodyCell`), so `UpdateWallMaskForBody` uploads the static mask only once at startup. The
  surface kernel must use the static mask only, or the surface rises over the submerged ball.
  Note the register pressure: `fluid_surface` already uses b1 for `GeometryConstants`, so the
  body constants need b2 there.
- **W-04** — one `CS_FluidReduce3D` pass produces α total, liquid-band headroom and liquid-band α
  into a small buffer; read back 16 bytes instead of 1.76 MB. Drop the "after" measurement
  outside debug. Persist the diagnostics buffer.

**Files** — every fluid kernel (`IsSolid`), `compute_kernels.json`, `FluidSimulation.{h,cpp}`,
new `CS_FluidReduce3D.hlsl`.

**Acceptance** — no "Fluid Wall Mask created" lines after startup; cavity depth ≥ 0.4 m,
submerged 0.60 at rest; self-test passes; frame-time max/p99 over 3000 frames before/after.

**Exit** — no per-substep mask upload, no full-volume readback every 15 frames.

---

### Stage 6b — Displace, don't delete; ball feels the water (W-07, W-06)

**Scope**
- **W-07** — a displacement kernel pushes α out of newly covered body cells into neighbours,
  preferring the body's velocity direction then upward, conserving the moved amount exactly.
- **W-06** — the ball uses the local simulated surface level for its spherical cap instead of the
  authored plane, and optionally drags against the local liquid velocity. Retune
  `sphereHeaveDamping` down once the ball reacts to real waves and document the new default.

**Files** — new displacement kernel, `CS_FluidReduce3D.hlsl` (column average),
`FluidSimulation.cpp`, `WaterTunables.h`, `BackyardScene.cpp`.

**Acceptance** — deficit during the 0.3 s after impact drops by ≥ half vs baseline; impact rim
height increases; ball at rest submerged 0.60 ± 0.02; dropping again over running waves shows
`y` correlating with the local surface (show both series).

**Exit** — the entry rim is physical and the body is coupled both ways.

---

### Stage 7 — Solver cost (W-15)

**Scope** — wrap `Step` in GPU profiling scopes; report solver GPU ms/frame at 1280×720 and
1920×1080. Only change item (1): fewer pressure iterations once the warm start has converged, with
a logged residual. Report single-dispatch red-black and multigrid-PCG with numbers, not
implementations.

**Files** — `FluidSimulation.cpp`, profiling scopes per
[gpu-performance-profiling-workflow.md](../rendering/gpu-performance-profiling-workflow.md).

**Acceptance** — before/after ms table; Stage 0 scene metrics unchanged.

**Exit** — the solver's cost is a known number, not an assumption.

---

### Stage 8 — Cleanup, record, platforms (W-14, W-16, W-17)

**Scope**
- **W-14** — deduplicate `FluidConstants` / `ObjectConstants` / the liquid threshold across the
  16 kernels. Check first whether the compute compile path resolves `#include`; if not, pass the
  threshold through constants and add a test that compares the cbuffer text of every fluid kernel.
- **W-16** — rewrite sections 6–8, 14 and 16 of the implementation record from section 2 of the
  catalogue and the `e90752b4` message; keep historical bugs, mark replaced fixes.
- **W-17** — run scene 7 and the fluid self-test on Android/Vulkan, Linux/Steam Deck and ARM64
  Windows; record results; file a task per failure.

**Acceptance** — build/self-test/scene unchanged after W-14; no statement in the record
contradicts the code; platform results recorded.

**Exit** — the branch is shippable and documented.

---

## 4. Stage log

Append measured results here per stage, so a cold session starts informed.

### Stage 0 — baseline (@ `01ccbd82`)

Built and measured 2026-10-28, D3D12 and Vulkan, x64 Release, 1280×720, scene 7,
`--logLevel debug`, `--dumpSnapshot-seconds 20`. **0 `[ERROR]` lines in both scene runs.**

#### Confirmed baseline (D3D12), matching the catalogue

| Metric | Catalogue | Measured |
|---|---|---|
| Still pool surface rms (pre-impact, t=0.32 s) | 0.1–0.3 mm | **0.1 mm** (`min=-0.0023 max=-0.0015 mean=-0.0018 rms=0.0001`) |
| Mean speed pre-impact | ≈ 0.006 m/s | **0.0028 m/s** |
| Impact frame (t=0.57 s) | — | `min=-0.5702 max=+0.0756 rms=0.0377` |
| Surface rms at t≈11 s | ≈ 2.2 cm | **2.19 cm** (sample 51) |
| Hollow under the floating ball | ≈ −35 cm | **−0.341 m** at rest |
| Water over the steps at rest | ≤ +5 cm | **+4.1 cm** |
| Ball at rest | submerged 0.60 | **0.60** (`y=-0.074 vy=0.000 t=19.8`) |
| Mean speed at rest (t≈19.5 s) | 0.0035 m/s | **0.0026–0.0032 m/s** |
| Volume correction deficit | ≈ 300 cells every 15 frames | **215–355 cells, last 274, constant from t=0** |
| Alpha volume drift | −0.03 % | **+0.011 %** (see finding 3) |
| Fluid self-test D3D12 | PASS | **PASS** (`projection ratio 0.388` at 50 sweeps, `massConservation drift -0.00%`, `walls solidSpeedSum=0 maxWallPenetration=0`) |
| `sim/wall ratio` | 1.00 | **0.99–1.00** |

#### Findings that contradict the catalogue

1. **Vulkan fluid self-test FAILS.** `[FluidSelfTest] Step 109 failed on API=vulkan`,
   `FAIL: failures=1`. Root cause logged immediately before: `[Vulkan] AllocateDescriptorSets
   failed res=-1000069000 frame=1 cachedSets=0` — that HRESULT is `VK_ERROR_OUT_OF_POOL_MEMORY`.
   The 400-frame selftest exhausts the descriptor pool; every earlier gate
   (massConservation, projection, walls) passes identically on Vulkan. The catalogue's
   "PASS D3D12 and Vulkan" is wrong. Filed as **W-18**.
2. **The Vulkan *scene* is healthy.** Scene 7 ran 20 s with **0 `[ERROR]` lines**, ball at rest
   submerged 0.60, surface rms 2.79 cm. So this is a harness/pool-sizing defect, not a solver
   defect — but it means the Vulkan gate is currently untrustworthy.
3. **Alpha-volume drift is +0.011 %, not −0.03 %.** The catalogue figure is stale. Drift is
   measured over 600-frame windows (`sim/wall ratio = 1.00 (600 frames)`), which at this frame
   rate is ≈ 2 s, not ≈ 20 s — so it is sampled far more often than assumed.
4. **Correction headroom is 0 for the first corrections** (`t=0.12 s headroom=0`) while the
   deficit is already 355 cells, and headroom only rises to ≈ 3400 at t≈19.8 s. The correction
   cannot be putting water where it was lost; this corroborates W-03.
5. **D3D12 and Vulkan are not comparable at rest** (rms 0.0205 vs 0.0279). With real-time dt the
   two runs accumulate different sim time per substep. Cross-backend comparison needs fixed-step
   mode, which is only a tunable today, not CLI-exposed.

#### Baseline gaps (feed directly into Stage 1 / W-12)

These metrics are **not instrumentable today**, so no later stage can be checked on them:

| Missing metric | Why it matters | Stage that needs it |
|---|---|---|
| Wave propagation (front speed, amplitude vs radius) | The transport and storage stages change dispersion/dissipation; nothing measures it | 5a, 5b |
| Boundedness violations (α outside [0,1]) | The whole point of the conservative scheme | 5b |
| Surface rms **excluding the ball region** | Current rms is dominated by the −35 cm hollow, so it cannot detect solver flatness regressions | 2, 5b, 6b |
| Ball trajectory (impact time, rebound peak, settle time) | Body-coupling stages can converge to 0.60 while changing dynamics | 6a, 6b |
| Substep/CFL distribution (p50/p99, % at cap) | A perf regression can hide as fewer substeps and read as "faster" | 7 |
| Frame/GPU ms | Performance has no baseline | 7 |
| Per-column floor depth | Optics depth is a constant today | 3 |
| Deterministic fixed-step CLI switch | Needed for any repeatable A/B | 1 |

`--backyardNoBall` (needed for the still-pool gate) does not exist yet either; the pre-impact
sample works around it for now because the ball takes ≈ 0.45 s to reach the water.

### Stage 1a — W-18 Vulkan descriptor pool

**DONE.** Root cause: `BeginFrame` early-returns while a frame is open, and a headless loop never
closes it, so the pool was reset once and then accumulated. Fix recycles a *compute-owned* frame
at 6144 sets (75% of maxSets) with a `WaitForGPU()` guard. Vulkan self-test now `PASS`
(`meanSpeed=0.1341`, 3 recycles); Vulkan and D3D12 scenes unchanged, 0 errors.
See the Stage 1a section in §3 for the full measurement table.

### Stage 1b — W-12 gates

**DONE (gates 1, 2, 3, 6).** Measured on both APIs:

| Metric | D3D12 | Vulkan | Gate |
|---|---|---|---|
| Still-pool mean | −10.5 mm | −10.5 mm | ≤ 12 mm |
| Still-pool rms | 0.1 mm | 0.1 mm | ≤ 5 mm |
| Still-pool mean speed | 2.4 mm/s | 2.4 mm/s | ≤ 10 mm/s |
| Water lost in 2 s | 0.693 % | 0.694 % | ≤ 0.80 % |
| Isotropy mirror | 0.65 mm | 0.01 mm | ≤ 3 mm |
| Isotropy axis | 10.3 mm | 10.4 mm | ≤ 16 / 12 mm |
| Impact / arrival at 0.5 m | 0.39 s / +0.04 s | 0.39 s / +0.04 s | 0.01–0.45 s |
| Mass endpoint drift | +0.007 % | +0.006 % | ≤ 0.05 % |
| Mass worst drift | +0.052 % | +0.054 % | ≤ 0.75 % |

Key findings: the correction deficit is **not** constant (202 → 749 cells over 2 s at rest with no
body, repaying only ~174 per check) and the pool sinks 1.1 cm/s; X-vs-Z anisotropy of 10.3 mm is a
property of the operator-split transport, not a bug. See the Stage 1b section in §3.

### Stage 1c — gates 4 and 5

_Pending._

### Stage 2 — W-01, W-08, W-13

_Pending._

### Stage 3 — W-09, W-10

_Pending._

### Stage 4 — W-11

_Pending._

### Stage 5a — W-02

_Pending._

### Stage 5b — W-03

_Pending._

### Stage 6a — W-05, W-04

_Pending._

### Stage 6b — W-07, W-06

_Pending._

### Stage 7 — W-15

_Pending._

### Stage 8 — W-14, W-16, W-17

_Pending._
