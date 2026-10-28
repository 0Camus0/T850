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
| 1. Make failures provable | W-12 | M | Turns every later fix into a gate instead of an opinion |
| 2. Cheap visible defects | W-01, W-08, W-13 | S–M | High visible payoff, low risk, no overlap with the solver |
| 3. Rendering correctness | W-09, W-10 | M | Independent of the solver; improves what already works |
| 4. Data-driven tunables | W-11 | M | Unlocks tuning without rebuilds; needed before solver tuning |
| 5a. 32-bit storage | W-02 | M | Prerequisite for the transport work |
| 5b. Conservative transport | W-03 | L | The remaining source of mass loss |
| 6a. Analytic body + cheap readback | W-05, W-04 | M | Removes per-substep uploads; unblocks 6b |
| 6b. Displacement + ball feels water | W-07, W-06 | M | Needs 6a's previous/current body state and async readback |
| 7. Solver cost | W-15 | M | Measure before optimising |
| 8. Cleanup, docs, platforms | W-14, W-16, W-17 | S–M | Only once the code is stable |

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

### Stage 0 — Baseline & guardrails

**Do**
- Build `T850.sln` / Release / x64 (`/m:1` if `-m:4` ICEs).
- Run `--fluid-selftest` on D3D12 and Vulkan.
- Run scene 7 at 1280×720 for ~20 s with `--logLevel debug`, capture the `[Backyard]` log lines.
- Fill in the §2.4 table of the catalogue with today's numbers.

**Acceptance** — 0 `[ERROR]` lines; self-test PASS on both APIs; the nine §2.4 metrics recorded.

**Exit** — baseline committed into section 4 of this file. No code changed.

---

### Stage 1 — Make failures provable (W-12)

**Scope** — the five gates plus the `--backyardNoBall` switch. Gate 1 (still pool) and gate 2
(isotropy) are the two that would have caught the bugs fixed in `e90752b4`; gate 3 is tightened
in Stage 5b.

**Files** — `DayScene/App.cpp`, `DayScene/BackyardScene.cpp`, `Framework/src/simulation/…`
(staged reads), possibly `Framework/src/debug/`.

**Acceptance** — all five gates pass at HEAD. Then demonstrate **two** gates actually fail when
their bug is reintroduced (catalogue requires the `id.y` revert and the wall-shell revert), and
restore.

**Exit** — gates in place and proven to be capable of failing. Every later stage cites them.

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

_Pending._

### Stage 1 — W-12 gates

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
