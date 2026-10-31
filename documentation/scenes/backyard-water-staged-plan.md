# Backyard Water — Staged Plan

Companion to [backyard-water-remaining-work.md](backyard-water-remaining-work.md) (the task
catalogue W-01…W-24, with current status in its section 0) and
[backyard-water-simulation.md](backyard-water-simulation.md) (the implementation record).

The task catalogue is the source of truth for **what** each task is and how to prove it. This
document is only about **order and session boundaries**, so each stage can be executed cold, in
one context, without carrying the whole backlog.

Stage 0 baseline: `01ccbd82`. Stages 0–4, 3c, 4a–4d and **5a** are done, plus the review fixes,
the debug view, pick and drag, `--unattended`, and the W-15 performance pass (`c911381a`) logged in
section 4. **Next: Stage 5b (W-03)**, starting from the tip of `backyard_scene`. Section 0 is the
step-by-step brief.

---

## 0. Next steps for the local agents (2026-10-30, after Stage 5a)

Start every session from the tip of `backyard_scene` and build:

```powershell
cd D:\Code\QwenFlashT850\T850-backyard\T850
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' T850.sln '/t:Framework;DayScene' /p:Configuration=Release /p:Platform=x64 /m:2 /nologo /v:m
```

If the build fails with C2011 `align_val_t` after a project or header change, delete
`Lib\VStuff\Framework\Release\x64\*.pch` and rebuild. Shader and JSON edits need no rebuild: assets
load from the source tree. Scene ids: 0 Sandbox, 1 DayScene, 6 Minecraft, 7 Backyard.

**Every automated run passes `--unattended`** (owner rule, 2026-10-30). The owner works on this
machine while tests run: a test window must never capture the mouse or take focus, and no test may
move the cursor or send keystrokes (`SetCursorPos`, `mouse_event`, `SendInput`, `keybd_event`,
`SetForegroundWindow`). Test input features in-process instead, the way `--backyard-pick-test` does.

**Regenerate the Backyard GLBs after pulling `9f6e7a2f`** (Blender command in the implementation
record 3.1). That commit fixes the water mesh's UV, which ran opposite to world Z and drew every
ripple and ball footprint mirrored across the pool (details in section 4, "Water mirrored in Z"). A
stale `Backyard_Water.glb` now logs `[Backyard] Water mesh UV is mirrored in Z` at startup.

### Done since the previous brief

- **Stage 3c (W-21)** — `5798477c`: the failure was uninitialised volume memory;
  `FluidSimulation::TryInitialize` now clears every volume. Accepted.
- **Stage 4 (W-11)** — `5511e7e9`: `Scenes/BackyardWater.json`, three promoted solver values, strict
  key validation. Accepted; the review added the missing Android registration (`e974f4e5`).
- Details of both, and of the review, are in section 4.
- **Stages 4a–4c (W-24 roof, W-23 textures, W-22 more balls)** — `2114fa0e`, `507ef1a4`, `0a981b8b`.
  Reviewer follow-ups: `9f6e7a2f` fixed the water drawn mirrored in Z (regenerate the GLBs, see
  above), and `97009aef` removed the ball cap — any number of balls, a new inline-buffer compute
  binding, and a **Spawn 10 balls** button (section 4, "No ball cap"). Both fixes were verified
  independently by the local agent (section 4, "Independent verification after the two review
  fixes"). **Stage 4d is done** (`e055efec`, section 4, "Stage 4d, part 2"): every ball gets a
  random palette material. **Next is Step 5 (Stage 5a).** Steps 1–4 below are kept for reference.
- **Owner requests after 4d (reviewer, section 4, "Pick and drag, PBR fixes, unattended runs")** —
  `c4c680dc` PBR lighting fixes in all three `FS_Quad` languages, `210cb571` `--unattended`, and
  `c920c9a5` Ctrl + left-drag moves a ball.
- **Performance pass (reviewer, section 4, "Performance pass and documentation audit")** —
  `c911381a` removed three CPU–GPU stalls: the D3D12 float-texture upload wait (engine-wide), the
  volume-correction readback (W-15 P1, most of W-04), and the wall-mask re-upload (W-15 P6, W-05).
  Stage 6a shrank accordingly. Measure with `backyard-diag\telemetry.ps1`, not FPS alone.
- **Documentation audit** — every engine document touched by these Framework changes now matches
  the code (list in section 4). Keep it that way: a Framework change ships with its doc update.
- **Stage 5a (W-02) is done** (section 4, "Stage 5a — W-02"). Part 1 (`14c705f4`) added the format;
  part 2 switched α, pressure and divergence to R32F. The gate shift that blocked part 2 was two
  test-side faults and one render artefact, not solver physics: the isotropy probe sat 9 cm off the
  impact, gate 5 leaned on fp16 noise, and the surface kernel read air trapped beside a floating ball
  as missing water. The surface kernel also got a groupshared tile (it recomputed every column 9×).
  Same session at 720p: D3D12 306.8 → 415.3 fps, Vulkan 353.4 → 402.3 fps (0 balls); with 30 balls
  266.7 → 377.0 and 327.2 → 360.2. **Next is Stage 5b (W-03)**, see "After that" below.

The owner then asked for three things before the solver work resumes, in this order (one stage per
session, smallest first). Full specifications are in remaining-work W-24, W-23 and W-22. After
those the owner added a fourth scene request — **a different PBR material on every spawned ball**
(Step 4, remaining-work W-25). The solver work (Stage 5a, W-02) follows as Step 5.

### Step 1 — Stage 4a, W-24: the house roof is upside down (small)

- One sign in `build_house()` (`T850/scripts/blender/build_backyard.py`): the roof slabs **and** the
  standing seams use `rot=(sign * angle, ...)`; it must be `-sign * angle`. A +X rotation raises the
  +Y end, so today both slabs lift their outer edge and the roof is a V.
- Close the two gable triangles that the corrected roof exposes (walls stop at the eave height).
- Regenerate with the Blender command in the implementation record 3.1. `backyard_layout.json` must
  not change (`git diff` empty). Prove it with a frame-300 back-buffer dump and the printed min/max
  Z of the slabs (eave 3.2 m, ridge 5.1 m).

### Step 2 — Stage 4b, W-23: real textures with normal maps

- Asset task: the engine already loads glTF base colour, normal (MikkTSpace tangents when missing),
  metallic-roughness, occlusion and emissive textures, embedded or external, with
  `KHR_texture_transform`.
- Source: ambientCG, CC0 (`https://ambientcg.com/get?file=<ID>_1K-JPG.zip`); use the `NormalGL` map,
  never `NormalDX`. Candidate IDs per surface are in W-23 — pick by preview.
- A fetch script with pinned IDs and sha256 downloads into an uncommitted cache; the Blender
  generator wires Image Texture → Principled BSDF / Normal Map, generates **UVs in metres**
  (world box projection; `smart_project` scale is arbitrary), exports tangents and embeds the
  JPEGs in the generated `.glb`.
- Do not texture or re-UV the water mesh. Layout JSON unchanged; scenes 7/1/6 on five APIs 0 errors;
  same-session fps before/after.

### Step 3 — Stage 4c, W-22: a button that spawns more balls

- The solver has one body. The design in W-22 stores the body index in the wall mask (`2u + k`;
  static solids stay `1u`; every reader only tests `!= 0u`), so the two body kernels look up
  `BodyVelocity[mask − 2]`. *Done; since `97009aef` the velocities are a per-dispatch structured
  buffer and there is no body cap — see W-22 "Follow-up fixes".*
- Host: body list in `FluidSimulation` (body 0 stays the scripted ball so the self-tests and gates
  7/8 are untouched), sphere–sphere contact, multi-body stamping, fastest-body substeps, volume
  correction summed over bodies.
- Scene: one render instance per ball sharing the ball primitive; random placement inside the basin
  minus radius and one cell, dropped straight down from above the water so it always lands in the
  pool; random radius (≥ 1.2 × cell) and density, mass derived and logged; `spawnSeed` for
  reproducible tests; new keys also go into the `glz::meta` in `BackyardWaterJson.cpp`.
- Measure fps with 0, 1 and 7 extra balls in one session: every footprint change re-uploads the mask
  (W-05), so the cost grows with the ball count.

### Step 4 — Stage 4d, W-25: a different PBR material on every ball (owner request) — DONE

**Done in `e055efec` by the reviewer** (section 4, "Stage 4d, part 2"). It differs from the plan
below in three places: the override is a plain `PrimitiveInst::InstanceMaterialMaps` flag, not a
`ShaderKey` bit (no new permutation to compile); the ball GLB declares the three maps itself as 1×1
placeholders, so the textured permutation and the tangents come from the normal glTF path; and the
owner asked for **random** materials, so a seeded shuffle bag replaces `(k - 1) % 32`.
**Gate 10 and the probe are now written** by the local agent (section 4, "Stage 4d, part 3"): the
gate is headless and checks the palette, the disk and decode of all 32 sets and the real shuffle bag;
the probe is a same-seed A/B against the placeholder look, so it proves the pixels, not the log.

Every spawned ball shows its own PBR material, from a palette of **32 verified CC0 sets**. The
palette table is in remaining-work W-25; the data lives in `Scenes/BackyardBallMaterials.json`.
**Owner decisions (2026-10-30):** palette stays at 32, textures at 512², texture files are fetched by
script and not committed, and the two local models are consulted at each step as usual. The owner
pointed the delivery at the `t850-textures` R2 bucket and asked that the cloud manifest stay in sync
with the local side, so the assets were published first (see section 4, "Stage 4d, part 1" — done);
the engine work below is what the next session executes.

- **Read the shader before designing around it.** `METALLIC_MAP` samples a *metallic-roughness*
  texture: `metallic = PBRParams.x * sample.b`, `roughness = PBRParams.y * sample.g`. So a palette
  entry is a packed ORM map (`R` = AO, `G` = roughness, `B` = metalness) which also carries the
  roughness — one per-instance texture drives both. `GLOSS_MAP` is the wrong path here (it reads
  `.r` and ignores the factor), and the per-subset factor cannot differ per ball, so the ball
  material's `metallicFactor` and `roughnessFactor` both become **1.0**.
- **AO is not available per instance.** `OCCLUSION_MAP` is gated on `MaterialParams7.x`, which the
  renderer fills from the *subset's* material, so an instance AO map does nothing. Skip AO for balls
  (a convex sphere needs none) and record the limitation.
- **The ball GLB has no UVs.** `Backyard_Ball.glb` carries `POSITION` and `NORMAL` only, so no
  texture can land on it. Add spherical `TEXCOORD_0` and `TANGENT`, white `baseColorFactor`,
  `metallicFactor`/`roughnessFactor` = 1.0, and regenerate with
  `python scripts\models\build_ball.py` — it writes glTF directly, no Blender needed.
- **Renderer.** `PrimitiveInst` already owns per-instance `Textures[28]`, and `PrimitiveInst::Draw`
  pushes them into the shared primitive then restores them — that is the established override
  mechanism. Only slot 3 (normal) is read from the instance today; add the base-colour (0) and
  metallic-roughness (6) fallbacks **behind a new `ShaderKey::INSTANCE_MATERIAL_MAPS` bit**, so the
  other scenes that put an atlas in instance slot 0 (Minecraft, Voxel) keep their current
  behaviour. Propagate the bit through `finalKey` the way `FLUID_SURFACE` is propagated.
- **Textures come from the cloud, not from ambientCG on every machine.** `.gitignore` ignores
  `Assets/Textures/Ball/`; only the authoring machine runs
  `scripts/textures/fetch_ball_materials.py` (1K archives → 512² `Color.jpg`, `Normal.png`,
  `ORM.png` with Pillow), and `scripts/PushCloudTextures.ps1` (new) uploads them to the
  `t850-textures` R2 bucket and republishes the shared manifest. Every other machine gets them from
  `scripts/DownloadTextures.ps1` like the sky cubemaps — see `documentation/development/cloud-assets.md`.
  **This half is already done** (see section 4, "Stage 4d, part 1"): 96 objects, 138 textures in the
  manifest, verified end to end. 512² RGBA8 is 1 MB per map decoded, so the palette is ~96 MB of
  texture memory; textures upload lazily per material at runtime, so the resident count is bounded
  by the palette, not by the ball count.
- **Proof.** Gate 10 (headless: palette ≥ 32, every entry resolves and loads on the running API, the
  assignment is a permutation for the first 32 balls) plus a frame-dump probe script that projects
  each ball's centre from `snapshot.json`'s `camVP` into the dumped back buffer (`RT_Dump_*.ppm`)
  and asserts the sampled colours are pairwise distinct.

### Step 5 — Stage 5a, W-02 (after 4a–4d) — DONE

**Done** (section 4, "Stage 5a — W-02"). The notes below are kept for reference.

Scope, files and acceptance: section 3 (Stage 5a) and remaining-work W-02. Before starting:

- **Record the performance baseline first, in this session.** On 2026-10-30 this machine measures
  D3D12 220 fps and Vulkan 315 fps at 720p with no spawned balls (the 2026-10-29 Vulkan drop was
  environmental and is gone). Still run `perf.ps1` on D3D12 and Vulkan before changing anything. 5a
  should *gain* frame rate — α, pressure and divergence go from 8-byte RGBA16F texels to 4-byte R32F
  (W-15's P2) — so report the delta against your own baseline, never against a number in these
  documents.
- **Blast radius.** Five compute backends plus the registry, the reflection map and the kernels. The
  solver runs on D3D11, D3D12 and Vulkan; on GL and WebGPU it reports "Fluid solver unavailable" by
  design, so those two need the format mapped (or rejected with a clear message), not a working
  solver. Scene 7 must still log 0 errors on all five APIs.
- **Vulkan storage images are write-only here** (implementation record section 4, constraint 1).
  Keep the existing pattern: write through `RWTexture3D<float>` with `[[spv::format_r32f]]`, read
  through `Texture3D<float>`.
- **Two commits.** First the format plus an R32F round trip in `ComputeSelfTest`
  (`--compute-selftest` on D3D11, D3D12 and Vulkan); then switch α, pressure, divergence and the
  clear kernel. A failure is then bisectable.
- Keep `ReadVolumeFraction`'s CPU output (one float per voxel) unchanged; the self-tests and
  `FluidDebugRenderer` consume it. **Do not touch the transport scheme** — that is 5b.
- After any Framework header change, delete the PCH before trusting the binary. In Stage 3c a stale
  binary silently kept the old behaviour.

### After that — the next stage

Stage 5b (W-03), the conservative transport. **Attempt 1 was measured and rejected on 2026-10-31**
(section 3, "Stage 5b, attempt 1"): operator splitting with upwind fluxes is exactly conservative and
bounded but three times more diffusive (interface 3.35 → 10.67 cells), so it was reverted rather than
shipped. The re-scoped target is a **less diffusive flux** — per-axis SLIC / Hirt–Nichols
donor-acceptor inside the one unsplit kernel — because the remaining defect is air smeared into the
bulk by first-order upwinding, not lost mass (total α already matches the body-adjusted reference
within 0.1 cells). Both candidate designs and their costs are in that section.

### Standing rules from the reviews

- **Test spatial changes off-centre.** The scripted ball drops on the pool's centre line, which is
  the axis of any mirror, so a flipped axis looks correct with it. Use `--backyard-spawn N` with a
  fixed `spawnSeed` and check that each ball's ripple is centred on the ball.
- **Self-tests:** `--fluid-selftest`, `--backyard-selftest` and every scene run with `--scene 7` (W-21
  is fixed; the scene-0 exception is gone).
- **Performance claims are same-session A/B only.** Measure the parent commit and the change in one
  session; `backyard-diag\bisect_perf.ps1 -Commit <sha> -Apis vulkan,d3d12 -Restore` builds the
  old tree, measures with `perf.ps1` and restores HEAD.
- **Register every new source file in all three build lists:** the vcxproj, the CMakeLists and
  `cmake/AndroidBuild.cmake`. `ValidateBuildRegistration.ps1` does not check Backyard or fluid files,
  so its PASS proves nothing here (Stage 4 missed Android this way).
- **Before adding a render pass, dump what the existing targets already hold.** Stage 3b re-drew the
  water into a new target that duplicated `Deferred:DEPTH`.
- **A zero-difference A/B is not a result until the target is shown to be non-empty and not
  saturated.** Stage 3b's CoC A/B measured an empty target (DOF off); with DOF on, `aperture: 90`
  saturated it. Revert temporary edits (DOF, aperture, extra dumps) before committing.
- **Every graph that runs `COC_PASS` binds slot 1** (the opaque depth again if it has no forward
  depth). WebGPU rejects an unbound declared texture, and Vulkan's placeholder texture is white.
- When a shared shader or graph changes, run `--scene 1` and `--scene 6` as well as `--scene 7`, on
  all five APIs, and quote the error counts.
- Commit locally on `backyard_scene`; never commit `T850/config.json`; never push.

**Hand-back:** append to section 4 under the stage heading: commit ids, before/after numbers, every
command run, and anything you could not verify.

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
7. **Always pass `--scene 7`.** `config.json` is only read with `--config`; a run without
   `--scene 7` tests SandboxScene. Check the log contains `[Backyard]` lines before trusting it.
8. **Look at the solver, not just the shaded result.** The ImGui "Debug view" (remaining-work
   section 4.5) shows the displaced mesh, the voxels and a velocity slice. Use it before and after
   any solver or surface change.

---

## 2. Stage map

| Stage | Tasks | Size | Why here |
|---|---|---|---|
| 0. Baseline & guardrails | — | S | Without a recorded baseline no later claim is checkable |
| 1a. Vulkan gate must be trustworthy | W-18 | S | **DONE** — Vulkan self-test now PASS |
| 1b. Make failures provable | W-12 | M | **DONE** — gates 1, 2, 3, 6 pass on both APIs |
| 1c. Readback gates | W-12 gates 4, 5 + boundedness | M | **DONE** — gate 5 proven to fail on the id.z bug |
| 2. Cheap visible defects | W-01, W-08, W-13 | S–M | **DONE** — GL/WebGPU 0 errors; lid proven open; dead sphere path deleted |
| 3. Rendering correctness | W-19, W-09, W-10 | M | **DONE, reviewed** — W-19 (`fa5422ff`), W-09 (`517ae6bf`), W-10 (`96d84638`, redesigned in review `47500166`: CoC consumes the nearer depth, no extra pass) |
| 3c. Vulkan fluid self-test after Backyard init | W-21 | S–M | **DONE** — root cause was NOT frame ownership: it was uninitialised GPU volume memory. Fixed in `TryInitialize` |
| 4. Data-driven tunables | W-11 | M | **DONE** — `Scenes/BackyardWater.json` drives all 101 settable fields; three solver literals promoted; strict key validation. See Stage 4 |
| 4a. House roof | W-24 | S | **DONE** — sign flipped, gables closed, 78 → 80 meshes, layout JSON unchanged |
| 4b. Textured materials | W-23 | M | **DONE** — CC0 ambientCG textures with NormalGL, metric world-box UVs, JPEGs embedded; −5.5 % fps; layout unchanged |
| 4c. Spawn more balls | W-22 | M–L | **DONE** — mask-encoded body index, gate 9 multi-body PASS, GUI spawn/clear. Review: Z-mirror fixed (`9f6e7a2f`), **cap removed** (`97009aef`, 100 balls tested) |
| 4d. Per-ball PBR materials | W-25 | M | **DONE** — 32 CC0 sets in R2 (part 1); `e055efec`: cube-sphere GLB with UVs, per-instance maps behind `InstanceMaterialMaps` (no new permutation), random shuffle-bag assignment, 0 errors on five APIs. Gate 10 not written |
| 5a. 32-bit storage | W-02 | M | **DONE** — `14c705f4` format on five backends; part 2 switched α/pressure/divergence to R32F, fixed the isotropy probe, the trapped-air moat and the 9× surface kernel. +35 % fps D3D12, +14 % Vulkan (0 balls) |
| 5b. Conservative transport | W-03 | L | The remaining source of mass loss |
| 6a. Analytic body + cheap readback | W-05, W-04 | S | **Mostly done in `c911381a`** — no mask upload after startup (GPU stamp), no correction readback. Left: W-04 steps 3 and 5 (persistent diagnostics buffer, async readback for 6b) |
| 6b. Displacement + ball feels water | W-07, W-06 | M | Needs previous/current body footprints and 6a's async readback |
| 7. Solver cost | W-15 | M | Measured (`3a371903`); P1 and P6 done in `c911381a` with a D3D12 upload fix. P3, P4, P5, P7, P8 here; P2 after W-02 (5a) and is now the largest lever |
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
   *Corrected in Stage 5a: this was a test bug. The annulus centre truncated 24.999998 to cell 24,
   9 cm off the impact in Z, and the "axis" value compares the −Z half with the +Z half (each
   quadrant spans both axes). Centred, both halves agree within 0.2 mm (fp16) / 0.6 mm (R32F).*
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

### Stage 1c — Remaining gates (W-12 gates 4 and 5) — **DONE**

**Scope delivered**

- **Gate 5 — surface-texture readback.** New `CS_FluidSurfaceToBuffer.hlsl` (Texture2D →
  `RWStructuredBuffer<float4>`) plus `FluidSimulation::ReadSurface`. Reads the texture the vertex
  shader actually displaces with, not the volume fraction — the volume cannot reveal a bug in the
  surface kernel itself.
- **Gate 4 — moving boundary.** New `FluidSimulation::ReadVelocity`. Body faces must carry the
  body's velocity; sampled deep-interior cells so the half-cell membership at the surface is not
  ambiguous.
- **α boundedness.** Count of cells outside [0,1], the direct target of the Stage 5b transport
  change.

**Measured (both APIs)**

| Metric | D3D12 | Vulkan |
|---|---|---|
| Boundary: body cells sampled | 47 | 47 |
| Boundary: worst face deviation | **0.00018 m/s** | 0.00018 m/s |
| Surface: median range along X | 4.5 mm | 4.4 mm |
| Surface: median range along Y | 2.2 mm | 2.2 mm |
| Surface: extreme value and position | −0.5576 m at x=56 y=24 | identical |
| Boundedness: cells outside [0,1] | **0** | **0** |

The directional probe is exact: ball dropped at x = +0.60 m (≈ column 56.7) puts the extreme at
**x = 56**, y = 24 against a centre of 25. A transposed mapping would put that offset on the Z
axis instead.

**Mutation proof — the gate really fails on the bug it exists for**

Reintroduced `id.z` in `CS_FluidSurface3D` (`if (id.x >= Width || id.z >= Depth)` and
`const int cz = int(id.z)`), rebuilt, ran:

```
surface texture  rowMedian=0.0016 m colMedian=0.0000 m  extreme=-0.0059 m at x=3 y=0 (centre 50,25)
ERROR surface texture is not a 2D field (0.0016 m along X, 0.0000 m along Y; need 0.0010 m in both)
FAIL: api=d3d12 failures=3
```

The across-Y median is **exactly zero** because every output row is a copy of grid column z = 0, and
the directional probe independently fails (extreme at x=3 instead of x=56). Restored the kernel,
rebuilt, ran: `PASS`, `colMedian=0.0022`, extreme back at x=56. This is the demonstration Stage 0
identified as missing from the original "verified" pass.

**Threshold calibration — GLM's 5 mm would have been wrong**

GLM proposed 5 mm on both axes. Measured medians are **4.5 mm along X and 2.2 mm along Y** on a
*correct* kernel, because a median over rows/columns is dominated by the far field where the
disturbance has decayed; a 5 mm threshold would fail a healthy solver. What matters is
discrimination: the bug produces **exactly 0** while still-pool noise gives ≈0.4 mm range over 50
samples, so 1 mm sits comfortably between them. This is the same class of mistake as the catalogue's
"constant ~300 cells" deficit — a threshold guessed rather than measured.

**Decision recorded** — Qwen argued against a CPU cross-check of the texture against a duplicated
elevation rule ("the risk is false negatives due to divergence… generally, no"); GLM wanted one at
a loose 5 mm tolerance. The directional probe covers the specific failure modes (wrong axis,
transposition) without duplicating the kernel rule on the CPU, so no cross-check was added. If a
wrong-integral bug appears later, it needs an analytic golden case rather than a duplicate rule.

**Exit** — all six gates pass on both APIs, and gate 5 is proven to fail on the exact bug class that
survived the earlier "verified" pass.

---

### Stage 2 — Cheap visible defects (W-01, W-08, W-13)

**Scope** (as planned)
- **W-01** — only set `FLUID_SURFACE` on the water instance once `TryInitialize` succeeded, and
  clear it on teardown. Removes the per-frame `GetShader miss` flood on GL/WebGPU.
- **W-08** — treat `j ≥ Height` as AIR in pressure/gradient/extrapolate instead of SOLID.
- **W-13** — delete `CS_FluidSphere3D.hlsl`, `InjectSphere`, `m_removedBuffer`,
  `BuildSpreadConstants`, the `fluid_sphere` registry entry and its pipeline creation, plus the
  unused `FluidSettings` fields. **Note:** that kernel carries the same `id.z` row bug the surface
  kernel had; deleting removes a latent trap.

**What actually had to change, and two deviations from the catalogue**

1. **The ceiling had to become a SETTING, not a global change.** Both agents predicted the risk:
   an unconditionally open top makes the +Y face of the top row a vent, so water can leave the
   domain and any test that assumes a closed box breaks. It did — the engine's synthetic
   `--fluid-selftest` (a 32×16×16 closed box, unrelated to the pool) failed with
   `velocity points into a wall face (max 0.630859 m/s)` at 8 pressure iterations. So the fix is
   `FluidSettings::openCeiling` (**default false**), opt-in per scene from
   `WaterTunables::openCeiling` (true for the Backyard pool), and both the pressure and gradient
   kernels branch on it via a new cbuffer field.
2. **Divergence needed no change.** The catalogue lists it, but on this staggered layout the +Y
   face of cell `(i,j,k)` *is* texel `(i,j,k).y`, already in range — there is no `j+1` texel to
   read. The lid came entirely from the gradient kernel's `IsSolid(b)` branch overwriting the face
   velocity with `SolidBoundaryValue() = 0`.
3. **Extrapolation needed no change.** It runs after the gradient and classifies the top face as
   solid, which means "do not extrapolate" — so it preserves the value the gradient just wrote.
   Treating it as an AIR face would have overwritten it with a neighbour average.
4. **A new cbuffer field is an R2 trap.** `FluidConstants` grew 64 → 80 bytes (`openCeiling` +
   3 pads, `static_assert` updated). Every one of the 17 fluid kernels declares the same five
   fields in the same order, and **the registry's per-binding `constant_count` had to go 16 → 20
   in all 16 entries at once.** With the struct grown but the registry left at 16, every single
   compute pipeline silently failed to create and the solver reported "resources are incomplete"
   with no underlying error — exactly the failure mode R2 warns about.

**Files** — `BackyardScene.{h,cpp}`, `BackyardSelfTest.{h,cpp}`, `WaterTunables.h`,
`FluidSimulation.{h,cpp}`, all 17 `CS_Fluid*.hlsl`, `compute_kernels.json`.

**Acceptance, as measured**

- **W-01**: `--api gl --scene 7` and `--api webgpu --scene 7` → **0 `[ERROR]` lines** (previously
  ~400 `GetShader miss` per 400 frames). Both log
  `[Backyard] Fluid solver unavailable; water renders as a flat plane with the ordinary material
  permutation (no FLUID_SURFACE)`. D3D12/Vulkan/D3D11 log
  `Water surface bound to solver texture (100x50), FLUID_SURFACE enabled` — so the bit is set
  exactly when the solver is up. All five backends: 0 errors.
- **W-08**: new **gate 6** in `BackyardSelfTest.cpp`. Heavy drop (density 3.0, 2.5 m) on the real
  pool grid; it probes the **+Y face velocity of a wet top-row cell**, which is the exact quantity
  the lid zeroed.
  - Open ceiling: `wet-top samples=15/60, top-face |v| max=0.7939 m/s` → PASS.
  - Ceiling closed (setting flipped to false, same binary path): `top-face |v| max=0.0000 m/s`
    **exactly** → `FAIL: the ceiling is a no-flux lid`. Mutation-proof through the whole chain
    tunable → settings → cbuffer → shader.
- **W-13**: build, `--fluid-selftest` (both APIs: PASS, wall penetration 0.000000 at 1, 8 and 50
  iterations), `--backyard-selftest` (both APIs PASS) and the scene unchanged. 26 → 25 kernels.

**Deviation — the catalogue's W-08 acceptance is not achievable as written.** It asks for "no α
accumulation in the top row". Measured: the top row reaches `alpha=1.000` and the peak column rise
is `+0.419 m` — the full 0.42 m of freeboard — *with the lid and without it, identically*. A
3x-density 0.25 m ball dropped 2.5 m into a 1.56 m deep pool genuinely overflows it, and the closed
domain has nowhere for the water to go. Filling the top row is therefore correct and cannot be the
failure signal; pinning against it is what is wrong. Gate 6 asserts the venting velocity instead,
and the top-row alpha is logged but not asserted. If the pool must not overflow, the fix is more
freeboard (Stage 4 / W-11 territory), not a solver change.

**Stage 2 numbers (unchanged from Stage 1c, so no regression):** still-pool rms 0.1 mm / meanSpeed
0.0024 m/s / volumeLoss 0.693%; isotropy mirror 0.00065 m axis 0.0103 m (D3D12) and mirror
0.00001 m axis 0.0104 m (Vulkan); mass endpoint drift +0.007% / worst +0.052%; boundary deviation
0.00018 m/s over 47 body cells; surface medians 4.5 mm (X) / 2.2 mm (Y), extreme −0.5576 m at x=56;
α violations 0.

**Exit** — three independent small wins plus one new gate; no change to the numerical scheme's
interior operator.


---

### Stage 3 — Rendering correctness (W-19, W-09, W-10)

**Scope**
- W-19 is **done** (`fa5422ff`): covered surface columns are inpainted in `CS_FluidSurface3D` and
  gate 7 guards it. Do not undo it while touching the surface kernel; gate 7 must stay green.
- Per-column static floor height texture (CPU-built from the static mask) so absorption uses the
  real depth over the steps instead of a constant 1.56 m. **DONE.**
- Depth test the refracted `SceneColorTex` lookup against `SceneDepthTex` so the ball and coping
  stop bleeding into the water. **DONE.**
- Blend the Fresnel normal toward the flat normal so crests change reflectivity without dark
  patches. **DONE.**
- W-10: a depth-only draw of the displaced water gives post effects a surface to see. **DONE**
  (details in 3b below).

**Files** — `FS_Mesh.hlsl`, `RenderMesh.cpp` (new texture slot — remember the per-slot `if`
branch, bug 18 in the record), `BackyardScene.cpp`, render graph JSON if a new target is needed.

**Acceptance** — A/B back-buffer dumps from one snapshot at t = 2.0 s: steps visibly lighter than
the deep end; no ball colour in the water; depth target contains the water surface. 0 errors on
D3D12/Vulkan/D3D11. Frame-time cost of the extra draw reported. For W-10, the "water mesh
wireframe" overlay must sit on the depth-only surface (same displacement), and gates 7 and 8 stay
green.

**Exit** — water optics match the geometry; no new passes disabled.

---

#### Stage 3a — W-09 water optics (done)

**What changed**

1. **Per-column floor depth.** `BackyardScene::BindFloorHeightTexture` builds a `width x depth`
   RGBA32F texture from the *authored* wall mask — floor Y = `basinMinY + solidRunFromBasinFloor *
   cellSize` — and binds it at **t24** (`FluidFloorTexture`), with `RenderMesh::Draw` naming the slot
   (bug 18). `FS_Mesh` then computes `columnDepth = waterPlaneY + elevation - floorY`, nearest-texel
   `Load` on purpose (bilinear across a step edge invents a ramp that never existed). The old
   `WaterOptics5.y` constant survives as the fallback when no texture is bound, gated by a new
   `WaterOptics5.w` flag (`WaterTunables::floorTextureBound`). Logged range: floor Y −1.600…−0.340,
   so depth **0.300…1.560 m**.
2. **Refraction depth test.** `SceneIsNearerAt()` compares the depth at the refracted UV with this
   fragment's own depth and falls back to the unrefracted UV. Hard switch, not a blend (GLM): the
   occlusion boundary is a geometric edge and cross-fading two lookups is what smears the coping.
3. **Fresnel normal.** `dot(blendedNormal, eyeDir)` replaces `eyeDir.y`, where the blended normal is
   the **solver** height-field normal (not the presentation ripple layer) lerped 50 % toward flat.

**Two bugs found while verifying — both real, both mine**

* **`MaxPrimitiveTextures` was 24, and slot 24 is one past the end.** `SetTexture(tex, 24)` wrote out
  of bounds and `RenderMesh` read out of bounds; the shader got floorY = 0 for every column, so
  `columnDepth` collapsed to the 0.02 m clamp and the **whole pool** brightened uniformly (+8.1
  levels). Fixed by raising the limit to 28 with headroom. Without the A/B this would have shipped
  as "looks brighter, must be working".
* **`Pos.z / Pos.w` is NDC z in [−1,1], not the [0,1] depth `SceneDepthTex` stores.** Comparing the
  two silently made every occlusion test fail (the refraction fix measured *exactly* 0 changed
  pixels before this). `hposition.z` is the rasterised depth in [0,1] and is the correct value.

**Measured** (deterministic A/B: `--regressionFixedDt 0.0033 --dump-frame 600`, same sim state, so
the only difference is the shader):

| Region | before | after | delta |
|---|---|---|---|
| deep end (left) | 126.71 | 126.89 | **+0.18** (unchanged, 1.56 m either way) |
| steps (right) | 122.52 | 125.20 | **+2.68** (now tinted as shallow) |
| pool mid | 125.58 | 125.48 | −0.10 |
| dry deck / sky / house | — | — | ≤ 0.04 |

9,055 pixels differ >12, and the large diffs sit in x 490…932, y 483…625 — the step side of the
pool. 464k of 921k pixels are bit-identical, which is the determinism proof.

**Honest note on the refraction fix.** Isolating it (`v2-refract` vs baseline at frame 200) changed
**zero** pixels, and instrumentation showed why: occlusion *does* fire, but only where the
displacement is tiny. Near the ball the surface is flat (W-19 already inpainted it), so
`refractOffset ≈ 0` there; where the ripples are strong there is nothing in front to occlude. With
`refractionStrength` at its default 0.025–0.03 the artefact is therefore latent, not visible. The
fix is kept because it is correct and cheap and becomes active as soon as a crest displaces the
lookup onto the ball or coping — and because without it those pixels sample whatever happens to be
in front.

**Fresnel:** measurable but small on calm water (0.844 in the ball region, 0.378 across the pool at
frame 600; 1.507/0.159 at frame 200). It is the mechanism that lets crests change reflectivity.

**Verification.** Release x64 builds; `--backyard-selftest` and `--fluid-selftest` PASS on D3D12 and
Vulkan with numbers unchanged from HEAD; scene 7 → 0 errors on D3D12, Vulkan, D3D11, GL, WebGPU
(floor texture bound exactly when the solver is up).

**Exit** — W-09 done; W-10 (depth-only water for post effects) still open.

---

#### Stage 3b — W-10 water depth for post effects (done, with one honest gap) — **superseded in review**

> **Review correction (`47500166`).** The pass described below was removed. The forward pass
> already writes the water into `Deferred:DEPTH` (`push: false`, `READ_WRITE`), identical pixel for
> pixel to this `WaterDepth` target (110,770 / 110,770 overlapping pixels on D3D12), so the extra
> pass only re-shaded the water (≈ 0.18 ms) and added water hidden behind opaque geometry. The
> "honest gap" was real and correctly reported: CoC was empty because Backyard ships with
> `dof_enabled: false` (and `aperture: 90` saturates CoC everywhere when it is on). CoC now samples
> the nearer of `GBuffer:DEPTH` and `Deferred:DEPTH`; verified with DOF on and aperture 5. Full
> numbers in remaining-work W-10. The two traps below are still true and worth knowing.

**What shipped.** A new render target `WaterDepth` (`color_count: 0`, `depth_format: F32`, screen
sized) and a new pass `Water Depth` right after `Forward Transparent`, which draws exactly one mesh
into it with `FORWARD_PASS`. `BackyardScene` dumps it as `RT_Dump_WaterDepth.ppm` for inspection.

**Two traps found while wiring it**

* **A forward-only subset can never draw in `RADIAL_DEPTH`, `GBUFFER` or `SHADOW_MAP`.**
  `ShouldDrawSubsetInPass` returns `!forwardOnly` for those three passes and `forwardOnly` for
  `FORWARD`, so the water is only ever drawn in the forward pass. The first attempt used
  `RADIAL_DEPTH_PASS` and produced a **completely empty** depth target; the signature has to be
  `FORWARD_PASS`.
* **`mesh_indices` must name the water, and a reordered asset list would silently break it.**
  `Backyard_Water` is the sixth asset (`mesh_indices: [5]`). `RebuildFluid` now logs an error if the
  water instance index is not 5, so a reorder fails loudly instead of drawing the ball into the
  depth map.

**Measured.** The depth target dump contains the water surface: **121,350 non-background pixels
confined to x 14…958, y 476…652** — the pool region, and nothing else. Cost of the extra pass,
median of 3 timed runs of 1200 frames at 1280×720 on D3D12: **6.884 s with it vs 6.669 s without =
+0.179 ms per frame (~3.2 % of a 5.6 ms frame)**. That is expensive for one mesh because the pass
runs the full `FS_Mesh` water shading and discards the colour; a depth-only fragment variant would
remove most of it (recorded as a follow-up).

**The honest gap — nothing consumes it yet.** The catalogue's fix also says "let the post passes
that need scene depth use the nearer of the two", and I could not verify that in this scene:

* `BackyardScene` **force-disables the DOF passes** (`SetPassEnabled("DOF", false)`, same for
  "DOF 2"), so depth-of-field cannot show the change;
* the `CoC` target, the natural intermediate, **dumps as all-zero** in this configuration — it is
  never written or is recycled before the dump, so an A/B on it proves nothing (I measured 0
  differing pixels with the water depth wired in, and 0 with it wired out: the test was measuring
  an empty target).

I therefore reverted the `FS_Quad` CoC change rather than ship shader arithmetic I could not
demonstrate working. Wiring a depth consumer is the remaining step, and it needs a DOF-enabled
scene to be verifiable.

**Verification.** `--backyard-selftest` PASS on D3D12 and Vulkan; scene 7 → 0 errors and the
`WaterDepth` RT created on D3D12, Vulkan, D3D11, GL and WebGPU.

---

### Stage 3c — Vulkan fluid self-test after Backyard initialisation (W-21)

**Scope** — find why `--scene 7 --api vulkan --fluid-selftest` fails when every other scene and
D3D12 pass, and fix the cause. The Backyard solver is initialised but never stepped before the
test, so look at what that initialisation leaves behind in the Vulkan driver's per-frame state.
Facts and the three diagnostics are in section 0, step 1; measurements and acceptance in
remaining-work W-21.

**Files** — expected `VulkanCompute.cpp` / `VulkanDriver.cpp` or `FluidSimulation.cpp`; the harness
(`App.cpp`) only if the investigation proves the sharing is harness-only.

**Exit** — the Vulkan fluid self-test passes under scene 7 and scene 0 with identical numbers.

---

### Stage 4 — Data-driven tunables (W-11)

**Scope**
- ~~Derive `gridWidth`/`gridDepth` from the layout~~ — **done in review** (one cubic cell size
  drives the grid; linked GUI sliders; `--backyard-cell-size`). See W-11 for the measurements.
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

#### Stage 4 — W-11 (done)

**My earlier "BLOCKED" note in this section was wrong, and the correction matters.** I had concluded
that a 107-field `WaterTunables` broke `DayScene.cpp`'s glaze instantiation and that the struct
needed splitting. That was a bad diagnosis from a confounded experiment: the failing build had
*several* changes applied at once, and the culprit was not the field count.

**What actually happened.** Field count was never the problem. Adding one probe field (105) built
clean; adding all three real fields (107) built clean. The build only failed when **glaze reached
`DayScene.cpp` through a header** — `WaterTunables.h` or `SceneDescriptor.h` — where MSVC dies in
`glaze/util/parse.hpp` (`C3878`). Keeping glaze out of those headers fixes it, with no struct split
needed. The lesson is the same one this project keeps re-learning: isolate one variable per
experiment.

**What shipped**

1. **Three solver literals promoted to tunables.** `alphaCompression` (was a `FluidSettings` literal
   0.5), `maxSubstepDt` (was 1/120) and `volumeCheckInterval` (was a hardcoded 15 inside
   `FluidSimulation` with no setter at all) are now `WaterTunables` fields, wired into
   `MakeFluidSettings` and pushed live every frame in `OnUpdate` via two new setters
   (`SetAlphaCompression`, `SetVolumeCheckInterval`). Four GUI sliders added.
2. **`Scenes/BackyardWater.json`** — the whole tunable surface (101 settable keys) authored in JSON.
   Loaded by a **dedicated leaf translation unit**, [BackyardWaterJson.cpp](../../T850/DayScene/BackyardWaterJson.cpp),
   which is the only place glaze meets `WaterTunables`. That file owns the explicit
   `glz::meta<WaterTunables>`, so glaze never reaches `DayScene.cpp`.
3. Stale "Fifteen float4 groups" comment corrected to 24.

**Six keys are deliberately not settable, each documented in the meta**

| Key | Why |
|---|---|
| `gridWidth` / `gridHeight` / `gridDepth` | derived from `cellSize` + the pool layout; the scene writes them back |
| `waterPlaneY` | owned by `backyard_layout.json` |
| `freeboard` | owned by the same layout; `FluidSimulation` overwrites it, so a JSON value would silently do nothing |
| `floorTextureBound` | runtime state the scene sets when the floor texture binds |

**Deviation from the catalogue: the file is `Scenes/BackyardWater.json`, not a `"water"` object inside
`BackyardScene.json`.** Reason: the scene descriptor's parse is strict and owns that file's top-level
key set, so teaching it a `"water"` key means putting glaze (or a `glz::raw_json` member) in
`SceneDescriptor.h` — which is exactly what breaks `DayScene.cpp`. A separate file keeps both
properties. Editing it is one JSON edit, which is what the acceptance asks for.

**Strictness: glaze's own unknown-key check cannot be used here.** `glz::opts{.error_on_unknown_keys
= true}` needs a compile-time perfect hash, and this glaze version's seed search fails at 101 keys
(`json/read.hpp(1892)`: `static_assert(bool(hash_info<T>.type))`). Confirmed by GLM: no knob exists.
So the read is permissive and the keys are validated by hand against
`glz::reflect<WaterTunables>::keys` — a typo is still a hard error that names the key, which is the
property that matters (Qwen's objection to a permissive read was exactly this, and the manual check
answers it).

**Measured**

| Check | Result |
|---|---|
| Shipped file loads | `19 of 101 keys set, the rest keep their C++ defaults`, 0 errors |
| **One JSON edit changes the run** | `pressureIterations` 60 → 20: log reads `20 pressure iterations`, and the `[columns]` surface probe changes (`-3 -3 -2 …` → `-1 -0 +0 …`) |
| **Unknown key is a hard error** | injected `sorOmegaTypo` → `Unknown water key 'sorOmegaTypo' … Known keys are listed in the glz::meta`, then `Startup/runtime failure: … invalid water tunables file`, process exit code **1** |
| Missing file falls back | renamed away → `No water tunables file … using the C++ defaults`, 60 iterations, 0 errors |
| **Full key round-trip** | generated a file with all **101** keys at their C++ defaults → `101 of 101 keys set`, 0 errors |
| `--backyard-selftest` | PASS on D3D12 and Vulkan |
| `--fluid-selftest` (scene 7) | PASS on D3D12 and Vulkan |
| Scenes 0/1/6/7 × 5 APIs | **0 errors in all 20 combinations** |

**Note for later stages:** `BackyardSelfTest` builds its settings from `WaterTunables{}` defaults, not
from the scene's JSON. That is deliberate — the gates must not change when the scene file is tuned —
but it means a JSON edit is not visible in the self-test numbers, only in a scene run.

**Recommendation** — do route 1 as a dedicated stage (call it **Stage 4a, "split the water
tunables"**), then Stage 4 proper becomes trivial: six small metas, each with `error_on_unknown_keys
= true` working normally, and one JSON object per group. Route 2 is a fallback if the split proves
too invasive.

**What is already proven and can be reused** (reverted, not committed, because it does not build):
the three `WaterTunables` fields, the two `FluidSimulation` setters, the `MakeFluidSettings`
wiring, the `OnUpdate` push and the four GUI sliders were all written and are correct — they only
need the struct split to land first.

---

### Stage 4a — House roof (W-24)

**Scope** — flip the roof-slab and seam rotation sign in `build_house()`, add the two gable walls,
regenerate.

**Files** — `T850/scripts/blender/build_backyard.py` only.

**Acceptance** — remaining-work W-24.

**Exit** — pitched roof with the ridge highest; layout JSON unchanged.

---

### Stage 4b — Textured materials (W-23)

**Scope** — CC0 textures (ambientCG) with normal maps on house, walls, ground, patio, pool shell,
coping and pergola; metric UVs; embedded in the generated GLBs.

**Files** — new `T850/scripts/textures/fetch_backyard_textures.py`, `build_backyard.py`; no engine
code. If an engine change turns out to be necessary (e.g. missing mips), stop and report it rather
than widening the stage.

**Acceptance** — remaining-work W-23.

**Exit** — every non-water surface textured at a believable scale; five APIs clean.

---

### Stage 4c — Spawn more balls (W-22)

**Scope** — body list in `FluidSimulation`, body index in the wall mask, `ObjectConstants` as
header + per-body velocity (cap 8), sphere–sphere contact, spawn/clear GUI and JSON tunables.

**Files** — `FluidSimulation.h/.cpp`, `CS_FluidGradient3D.hlsl`, `CS_FluidObstacleVelocity3D.hlsl`,
`compute_kernels.json`, `BackyardScene.h/.cpp`, `WaterTunables.h`, `BackyardWaterJson.cpp`,
`Scenes/BackyardWater.json`.

**Acceptance** — remaining-work W-22. The default run (no spawned balls) must reproduce every
self-test number exactly.

**Exit** — up to 8 balls at once, all landing in the pool, floating or sinking by their density.

---

### Stage 4d — Per-ball PBR materials (W-25)

**Scope** — a data-driven palette of 32 CC0 PBR sets; each spawned ball (and the scripted ball)
takes a different one. Per-instance base colour, normal and packed metallic-roughness maps; ball
GLB gains UVs and tangents; a fetch/pack script produces 512² runtime textures; gate 10 proves the
palette and the assignment; a frame-dump probe proves the materials differ on screen.

**Files** — `Framework/Descriptors.h` (new `ShaderKey::INSTANCE_MATERIAL_MAPS` bit),
`Framework/src/scene/PrimitiveInstance.cpp` (`SetGlobalKey` merge), `Framework/src/scene/RenderMesh.cpp`
(instance-slot fallbacks for slots 0 and 6, normal slot 3 preferred when the bit is set, `finalKey`
propagation), `scripts/models/build_ball.py` (UVs, tangents, factors 1.0),
`scripts/textures/fetch_ball_materials.py` (new), `Scenes/BackyardBallMaterials.json` (new),
`DayScene/BackyardScene.h/.cpp` (palette load, lazy texture creation, per-ball assignment),
`DayScene/BackyardSelfTest.cpp` (gate 10), `scripts/validation/probe_ball_materials.py` (new),
`.gitignore` (`Assets/Textures/Ball/`), docs.

**Acceptance** — see remaining-work W-25. Additionally: gates 1–9 unchanged, self-tests PASS on
D3D12 and Vulkan, scene 7 (and scenes 0/1/6) 0 `[ERROR]` lines on all five APIs, and the same-session
fps delta reported for 0 / 7 / 32 balls.

**Risks** — the material factors are shared per subset, so every ball must be map-driven (any ball
without a map falls back to a flat matte-metal look, which is why the fetch step is mandatory).
Shader-permutation count grows by the ball variants only.

**Exit** — 32 balls spawn with 32 visibly different materials; `BackyardBallMaterials.json` lists
them; a missing texture file logs one warning naming the fetch script and does not abort the scene.

**Outcome** — done in `e055efec`; the files actually touched are `PrimitiveBase.h`,
`PrimitiveInstance.h/.cpp`, `RenderMesh.cpp`, `build_ball.py`, `BackyardWaterJson.h/.cpp` (palette
loader, so no new build registration) and `BackyardScene.h/.cpp`. `Descriptors.h` is untouched.
See section 4, "Stage 4d, part 2".

---

### Stage 5a — 32-bit storage for α, pressure, divergence (W-02)

**Part 1 is done (`14c705f4`).** `ComputeStorageFormat::R32Float` exists; the registry parses
`r32float`, the SPIR-V reflection maps `SpvImageFormatR32f`, and each of the five backends resolves
a storage format in **one helper** instead of a per-site ternary. The binding checks now also compare
a 3D volume's texel format with the shader's declared storage format on D3D12 and Vulkan; that
mismatch was silent garbage before (undefined behaviour on Vulkan with no validation layers). The
compute self-test runs the same 3D workload as RGBA16F **and** R32F, and the R32F kernels write
`value + 1/3` with a `1e-4` tolerance, which fp16 cannot hold, so a backend that quietly stores 16-bit
fails: PASS on D3D12, Vulkan and D3D11 with `mismatches=0, visited=3927` on the 33×17×7 case.

**Part 2 is done** (section 4, "Stage 5a — W-02"). It had been held back because three calibrated
gates moved when α, pressure and divergence switched; the measurements below are the other agent's,
and section 4 explains each one. None was solver physics, and no limit was relaxed to pass:

| Gate | RGBA16F | R32F, as first measured | Cause | R32F now |
|---|---|---|---|---|
| early isotropy ("X vs Z", +0.30 s) | 10.7 mm | 29.2 mm | probe 9 cm off the impact in Z | Z mirror 0.5 mm (limit now 3 mm) |
| gate 7 `ring-far` | −1.2 mm | −17.1 mm | trapped air read as missing water | −4.9 mm (limit 15 mm) |
| gate 5 surface-texture Y median | 2.4 mm | 0.8 mm | the 1 mm limit leaned on fp16 noise | 0.7 mm (limit now 0.25 mm; the bug reads 0) |
| still-pool loss over 2 s | 0.693 % | 0.001 % | fp16 rounding | 0.001 % (limit now 0.05 %, was 0.80 %) |

**Original scope** — add `R32Float` to `ComputeStorageFormat`, the registry parser, the reflection map,
and every backend that maps or validates formats. `CreateVolume` takes a format; α, pressure and
divergence become R32F; velocity stays RGBA16F. Kernels that write them switch to
`RWTexture3D<float>` with `[[spv::format_r32f]]`; readers to `Texture3D<float>`. The clear kernel
needs an R32F permutation (it clears both kinds). Extend `ComputeSelfTest` with an R32F round
trip.

**Files** — `BaseDriver.h`, `ComputeKernelRegistry.cpp`, `ComputeReflection.cpp`,
`D3D11/D3D12/Vulkan/GLCompute.cpp`, `WebGPUDriver.cpp`, `RenderGraph.cpp`, `FluidSimulation.cpp`,
`compute_kernels.json`, all α/pressure/divergence kernels, `ComputeSelfTest.cpp`. Keep the CPU
output of `ReadVolumeFraction` (one float per voxel) unchanged: the self-test and
`FluidDebugRenderer` consume it.

**Acceptance** — compute self-test passes on D3D11/D3D12/Vulkan; fluid self-test passes. Report
the `VolumeCorrection` deficit before/after and surface rms at t ≈ 11 s.

**Exit** — 32-bit fields live on all compute backends. **Do not touch the transport scheme in
this stage.**

---

### Stage 5b — Conservative, bounded transport (W-03)

**Baseline measured 2026-10-31 at `484443c9`** with `--backyard-mass-audit 60 --regressionFixedDt 0.0033
--dump-frame 3000` (scripted 0.25 m / 39.27 kg ball, 4.43 m/s impact), D3D12 and Vulkan agreeing to the
last digit because the run is deterministic:

| t | total (cells) | ref | deficit | bulk mean | bulk min | below 0.98 | interface width (mean / max / columns) |
|---|---|---|---|---|---|---|---|
| 0.21 s (before the drop) | 82 941 | 82 971 | **+30.2** | 0.9997 | 0.9991 | 0 of 76 304 | 0.00 / 0 / 0 |
| 0.61 s | 82 881 | 82 971 | +89.8 | 0.9995 | 0.8062 | 184 | 3.39 / 8 / 216 |
| 1.99 s | 82 946 | 82 971 | +24.8 | 0.9985 | 0.5798 | 1 229 | 2.51 / 13 / 3 013 |
| 3.97 s | 82 907 | 82 971 | +64.1 | 0.9977 | 0.5359 | 2 620 | 2.69 / 13 / 5 000 |
| **8.73 s (steady)** | **82 917** | 82 971 | **+53.8** | **0.9965** | 0.7502 | **4 056 of 76 276 (5.3 %)** | **3.49 / 13 / 5 000** |

The same audit with the shipped **120 sweeps** (scene 7, wind and ball on, D3D12; Vulkan agrees):

| t | total | ref | deficit | bulk mean | bulk min | below 0.98 | interface width (mean / max / columns) | div rms |
|---|---|---|---|---|---|---|---|---|
| 0.21 s | 82 966 | 82 971 | **+4.4** | **1.0000** | 0.9999 | **0 of 76 304** | 0.00 / 0 / 0 | 0.0000 |
| 8.13 s | 82 917 | 82 971 | +53.5 (all body) | 0.9969 | 0.7571 | 3 998 | 3.35 / 13 / 5 000 | 0.0313 |
| 8.33 s | 82 917 | 82 971 | +54.0 (all body) | 0.9968 | 0.7502 | 4 036 | 3.37 / 13 / 5 000 | 0.0318 |

So the **initialisation transient is gone** (30 → 4.4 cells, bulk exactly 1.0 at t = 0.21 s) and the
interface is a little sharper, but the **post-impact dilution is unchanged** — that part is the
transport's own clip under residual divergence during the splash, which is what the per-axis scheme
below has to fix.

The interface width is the number to watch for the transport itself: 3.49 cells on average with a
13-cell maximum, over all 5 000 columns of a pool whose surface should be 1–2 cells thick.

**Two measurement faults found and fixed before the transport work (2026-10-31).** (a)
`MeasureVolumeFraction` summed 106 304 float32 values in float, losing ~25 cells and inflating the
seed reference by the same amount; it now sums in `double`, and gate 1's honest still-pool loss is
0.013 % (was reported as 0.001 %). (b) The 30-cell early loss is **projection residual, not the
boundary and not upwind diffusion**: the per-cell diff shows −21.47 cells spread evenly over 48 112
cells with **zero** cells gaining, i.e. the `saturate()` clip — cells with net outflow lose α to
neighbours that are already full. The iteration sweep proves it (still pool, no ball, no wind):
deficit **+29.6 cells at 60 sweeps, +4.4 at 120, +0.1 at 240**, for D3D12 397 / 298 / 189 fps at
720p. So the transport conserves exactly *given a divergence-free field*, and W-03 has two levers:
the per-axis conservative dilation (no clip at all) and more sweeps once W-15 P2 makes them cheap.
The audit reports the divergence residual (`div rms`/`max`) and the saturated-cell count, so the
clip can be attributed in one line. **Also: compare the deficit against the body-adjusted target.**
At 8.73 s the ball floats with `submerged = 0.60`, so the correction's target is
`ref − 53.8 = 82 917` and total alpha sits exactly there: the "+53.8" is the displaced body volume,
not lost water.

**Sweeps raised to 120 on 2026-10-31** (`WaterTunables`, `Scenes/BackyardWater.json` and
`BackyardSelfTestOptions` together, so the gates validate the shipped setting). Same-session A/B at
720p with 0 balls: D3D12 **401.9 → 295.4 fps** (`scene.update` 1.53 → 2.45 ms), Vulkan
**391.6 → 274.2**; 30 balls 273.0 / 255.5. Gates improved rather than just passing: still-pool loss
0.013 % → **0.002 %**, mean offset −0.2 → **−0.0 mm**, `maxAbs` 0.3 → 0.1 mm. The float gates moved
toward their limits (`ring−far` −4.9 → −5.6 mm against 15, `covered min−ring` −15.9 → −17.3 mm
against 20) because the sharper projection draws a stronger meniscus; W-26 should recover that
margin.

**Acceptance** (unchanged from W-03) — deficit < 20 cells per 15 frames over 3000 frames, at rest
**and** after impact; mass drift ±0.05 %; surface rms ≤ 2.5 cm at t ≈ 11 s; bulk mean α ≥ 0.999 with
< 1 % of bulk cells below 0.98 before and 10 s after the impact; rings still render. Measured with
`--backyard-mass-audit`, not by eye.

---

### Stage 5b, attempt 1 — operator splitting was measured and rejected (2026-10-31)

**What was built.** `CS_FluidAlphaAdvectAxis3D.hlsl`: one axis per dispatch, three per substep with
the order alternating, every face flux a single number both cells use (exactly conservative) and a
donor/receiver limiter that caps each cell's outflow at its own α and its inflow at its headroom, so
α cannot leave [0, 1] and no `saturate()` is needed. Full host plumbing (axis in the constants'
`Iteration` slot, ping-pong between the two α volumes) and a JSON switch.

**What it measured** (same binary, one flag, deterministic, D3D12 = Vulkan, t ≈ 8.2 s after a
4.43 m/s impact):

| | deficit | bulk mean | below 0.98 | interface width | saturated cells |
|---|---|---|---|---|---|
| unsplit upwind (today) | +53.5 | 0.9969 | 3 998 | **3.35** | 47 636 |
| operator-split + limiter | +53.8 | 0.9950 | 4 777 | **10.67** | 343 |
| split, `alphaCompression` 1.0 | +47.4 | 0.9955 | 3 828 | **12.63** | 1 235 |

So the split path is **exactly as conservative** (deficit unchanged — it is the displaced body
volume) and **bounded** (343 cells at α ≥ 0.9999 rather than 47 636, i.e. nothing is being clipped at
1), but it is **three times more diffusive** and dilutes the bulk slightly more. Three sequential 1D
upwind passes per substep apply roughly three times the numerical diffusion of one unsplit 3D pass;
raising the compression term makes it worse, not better. It fails the acceptance on the metric that
matters and was reverted rather than shipped behind a switch.

**Transport sub-cycling, also measured.** Running the *existing* unsplit pass 1, 3 or 6 times per
substep (same projected velocity, dt divided, dispatches only — no extra pressure work) changed
nothing: interface 3.35 / 3.40 / 3.37 cells, bulk 0.9969 / 0.9967 / 0.9968, deficit +53.5 / +53.8 /
+53.5. The advection is not the diffusion source at this step size.

**What actually moves the metric: solver substeps.** Halving the substep length sharpens the surface
measurably, because the projection is then far more converged for the step it drives:

| `maxSubstepDt` | substeps at 180 fps | bulk mean | below 0.98 | interface width | div rms | D3D12 / Vulkan fps |
|---|---|---|---|---|---|---|
| 1/120 (shipped) | 1 | 0.9968 | 4 036 | 3.37 | 0.0318 | 295 / 274 |
| 1/360 | 3 | 0.9976 | 2 980 | 3.08 | 0.0095 | 111 / 58 |
| 1/720 | 6 | 0.9984 | 1 966 | **2.57** | 0.0038 | — |

That is the CFL/divergence lever, and it costs 3–6× the whole solver (each substep re-runs all 120
pressure sweeps), which is why it cannot be the fix.

**Conclusion — W-03 is re-scoped.** Mass is *not* the remaining problem: total α sits at the
body-adjusted reference to within 0.1 cells, and the correction is effectively idle. What the audit
calls "dilution" is air smeared into the bulk by the **first-order upwind flux**, i.e. a
distribution error, and no conservation or clipping change can touch it. The acceptance numbers
(bulk ≥ 0.999, interface 1–2 cells) need a **less diffusive flux**, which means interface
reconstruction. Two candidate designs, in order of expected value per line of code:

1. **Per-axis SLIC / Hirt–Nichols donor-acceptor flux** inside the existing unsplit kernel: decide
   per face whether the fluid sits against the face or at the far side from the two neighbour α
   values, then send `min(α_donor, cfl)` or `max(0, α_donor − (1 − cfl))` instead of `|u|·α_donor`.
   Cheap (a few loads), keeps one dispatch, and is the classic fix for exactly this smear. Needs a
   boundedness guard for the far-side case.
2. **Two-pass FCT on the same stencil**: pass 1 computes each cell's raw out/in sums into a scratch
   3D texture, pass 2 scales every face by `min(λ_donor, λ_in_receiver)`. Strictly conservative and
   bounded, but it changes only the clipping, not the diffusion, so on its own it will not move the
   acceptance numbers.

Do (1) first and measure with `--backyard-mass-audit` at t ≈ 8 s; keep (2) only if clipping shows up
again once the interface is sharp.

**Original scope (superseded — see the attempt-1 result above).** Replace the upwind + `saturate` clip
with either the Weymouth–Yue operator-split dilation scheme or a Zalesak-style flux limiter on the
existing unsplit form. Keep the global correction as a safety net and log its deficit; it should
approach zero.

**Files** — `CS_FluidAlphaAdvect3D.hlsl` (+ possibly new per-axis kernels),
`compute_kernels.json`, `FluidSimulation.cpp`.

**Acceptance** — deficit < 20 cells per 15 frames over 3000 frames, at rest **and** after impact.
Self-test mass drift within ±0.05%. Surface rms at t ≈ 11 s ≤ 2.5 cm. Rings still render. Tighten
gate 3 of W-12 to the new threshold. Debug view "bulk water" mean α ≥ 0.999 with fewer than 1 % of
bulk cells below 0.98, before and 10 s after the impact.

**Exit** — mass loss is solved at the source, not patched by the correction.

---

### Stage 6a — Analytic body and cheap readback (W-05, W-04)

**Status (`c911381a`).** The exit criteria below are met by a different design: the mask is
restamped on the GPU (`CS_FluidStampBodies3D`) instead of making the body analytic, and the
correction's sums and deficit stay on the GPU (`CS_FluidVolumeReduce3D`) instead of a 16-byte
readback. What remains is W-04 step 3 (persist the diagnostics buffer) and step 5 (asynchronous
readback, which W-06 in 6b needs). The original plan follows for reference.

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

**Scope** — measurement is done (W-15 table; the solver is 58–63 % of the frame). Implement the
engine-side findings P3 (D3D12 barrier batching), P4 (Vulkan descriptor-set cache), P5 (D3D12
per-dispatch CPU work), P7 (surface written in place) and P8 (unused template passes), one
commit each. P1 and P6 are done (`c911381a`); P2 belongs to stage 5a. The W-15 profiling pass
shows the cost is now ≈ 127 dependent dispatches at ≈ 10 µs each, so P2 matters most and D3D12
barrier batching alone measured no gain.

**Files** — `D3D12Compute.cpp`, the Vulkan compute dispatch, `FluidSimulation.cpp`,
`BackyardScene_RenderGraph.json`. P3–P5 are engine-wide: also run `--compute-selftest` on all
APIs and a DayScene scene that uses compute post-processing.

**Acceptance** — `perf.ps1` before/after per item on D3D12 and Vulkan; all self-tests PASS; Stage 0
scene metrics unchanged. Note the profiler pitfall in W-15: solver GPU scopes are dropped.

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
property of the operator-split transport, not a bug (*corrected in Stage 5a: an off-centre probe*).
See the Stage 1b section in §3.

### Stage 1c — gates 4, 5 and boundedness

**DONE.** Boundary face deviation 0.00018 m/s over 47 body cells; surface texture medians 4.5 mm
(X) / 2.2 mm (Y) with the extreme at x=56 for a ball dropped at x=+0.60 m; alpha violations 0.
Mutation test: reintroducing id.z made the across-Y median **exactly 0.0000 m** and the gate
failed with 3 failures; restoring it returned PASS. Thresholds calibrated to 1 mm because GLM's
suggested 5 mm would fail a correct kernel (measured medians are 4.5 and 2.2 mm).

### Stage 2 — W-01, W-08, W-13

Done. W-01: FLUID_SURFACE is now set only after `TryInitialize` succeeds (`SetWaterFluidFeature()`
in `RebuildFluid`), and cleared on teardown or when the solver is unavailable — GL and WebGPU go
from ~400 `GetShader miss` errors per 400 frames to **0**, and the water draws with the ordinary
material permutation. W-08: the open ceiling is a **setting** (`FluidSettings::openCeiling`,
default false, enabled from `WaterTunables` for the pool) because making it global broke the
engine's synthetic closed-box test (`velocity points into a wall face, 0.630859 m/s`). Only
pressure (Dirichlet `diag += 1`, not a theta-derived ghost when the row above has no alpha) and
gradient (the `IsSolid(b)` branch that zeroed the top face) needed changing — divergence never
read out of range here, and extrapolate's "solid" classification is what preserves the gradient's
value. New cbuffer field cost: struct 64 → 80 bytes and **every registry `constant_count` 16 → 20
at once**; without that all 16 compute pipelines fail silently. Gate 6 (heavy 3.0-density 2.5 m
drop, wet-top-row +Y face velocity): open `0.7939 m/s` → PASS, closed `0.0000 m/s` exactly → FAIL,
so the whole tunable → settings → cbuffer → shader path is mutation-proven. Catalogue acceptance
deviation: the top row legitimately fills to `alpha=1.000` and rise `+0.419 m` in *both*
configurations — a 3x-density ball into a 1.56 m pool overflows it — so the gate asserts venting
velocity, not top-row water content. W-13: `CS_FluidSphere3D.hlsl`, `InjectSphere`,
`m_removedBuffer`, `m_removedColumns`, `BuildSpreadConstants`, the `fluid_sphere` registry entry,
its pipeline, and the dead `FluidSettings` fields (`pressureScale`, `initialFill`,
`wetThreshold`) and `FluidSimulation::Reset` are gone; 26 → 25 kernels. All six gates plus gate 6
PASS on both APIs with Stage 1c numbers unchanged; `--fluid-selftest` PASS on both; five backends
0 errors on scene 7.

### Review of stages 0–2 (2026-10-28) — fixes applied by the reviewer

Re-run independently: all gates PASS on D3D12 and Vulkan, GL/WebGPU 0 errors. The reviewer
committed the changes below directly, so **pull `backyard_scene` (at or after `5d640c2e`) before Stage 3**:

1. `d2291f1a` — the gradient kernel used an α-derived θ (≈ 0.5) for the open-ceiling face while
   the pressure kernel solves it with θ = 1, doubling the pressure step there. Gradient now uses
   ghost pressure 0. Gate 6 top-face |v| max **0.7939 → 0.1478 m/s** (still ≫ 0.01 threshold).
   Correction to the Stage 2 note above: the ball does **not** overflow the pool (0.065 m³ over
   40.5 m² = 1.6 mm); the +0.419 m is a local splash crown. Freeboard stays 0.42 m; expose it in W-11.
2. `fa5422ff` — W-19, the funnel around the floating ball. Covered surface columns are inpainted
   from the surrounding free surface; new gate 7. Before/after: covered texels −33.8 → −0.3 cm
   relative to the ring, ring −2.27 → −0.61 cm (D3D12) / −0.81 cm (Vulkan) relative to the far
   field. The scene's `[surface]`/`[columns]` lines now read the rendered texture, so the "Hollow
   under the floating ball ≈ −35 cm" row in the Stage 0 table no longer applies and the "surface rms
   excluding the ball region" gap is closed by gate 7.

3. `33baad50` — flicker follow-up to W-19: the first W-19 kernel used an α ≥ 0.5
   test to decide "body here", which flipped while the ball bobbed — 0.44 m frame-to-frame jumps next
   to the ball. Body detection is now mask-only with a continuous blend; new gate 8 (frame-to-frame
   jerk near the settling ball, limit 1 cm) measures 0.0066 m and fails on the old kernel.
4. `4d21e836` — fluid debug view (tooling, no solver change): ImGui "Debug view" overlays for the
   displaced mesh wireframe, solver surface grid, liquid/partial/solid voxels, a velocity slice and
   the grid bounds. Option table in remaining-work section 4.5. Costs nothing while off.
5. `d6b65147` — W-11 item 3: the grid follows one cubic cell size (`FluidSimulation::DeriveGrid`); width,
   depth and cell-size sliders are linked and rebuild on release; `--backyard-cell-size <m>` runs
   the self-test on another grid. Default grid unchanged (100×22×50, all gates PASS).
6. `3a371903` — W-15 measured: the solver is 58 % (D3D12) / 63 % (Vulkan) of the frame; D3D12 is
   GPU-bound in it, Vulkan CPU-bound. Fixed the per-substep surface work and two log-only GPU
   stalls: 720p D3D12 191 → 219 fps, Vulkan 298 → 313 fps, results unchanged. Findings P1–P9 with
   expected gains are in W-15; frame-rate A/B script `backyard-diag\perf.ps1`.
7. `93dff3d9` — the debug view's voxel layers crashed D3D12: dynamic vertex buffers stream through
   a 16 MB per-frame ring, and "partially filled cells" drew the whole diluted pool (≈ 81k boxes,
   15.6 MB). Layers are now capped (8000 boxes, evenly strided), partial cells are interface-only,
   and a new "bulk water" line reports the dilution (see the second lead below).
8. `5d640c2e` — debug view: velocity arrows for the whole water volume (speed-coloured, minimum
   speed filter, auto lattice step, 15 000-arrow cap). At rest ≈ 56k of 86k liquid cells still move
   faster than 5 mm/s.

Any change to the debug renderer must keep every dynamic upload bounded: test with all layers on
at 1440p on D3D12 and confirm 0 `Ring buffer overflow` lines.

**Lead for Stage 5b (W-03): the still-pool sink depends on where the waterline falls in a cell.**
Running the still-pool gate at several cell sizes (2 s, D3D12):

| Cell | Waterline cell fill = frac(1.56 m / h) | Mean at 2 s |
|---|---|---|
| 0.09 | 0.33 | −10.5 mm, still sinking |
| 0.06 | 0.00 | −3.0 mm, steady from 0.2 s |
| 0.075 | 0.80 | −0.4 mm, steady |
| 0.1216 | 0.83 | −0.4 mm, steady |

Only the grid whose still-water cell is **less than half full** sinks. A cell with α < 0.5 is AIR
to the pressure solve, so its water has no pressure support. Hypothesis, not proven: confirm it
first (e.g. move the authored water plane by a fraction of a cell at 0.09 m) before designing
the W-03 fix around it. The default grid is not changed to hide it.

**Second lead for Stage 5b: the bulk water is diluted.** The debug view's "bulk water" line (liquid
cells with no air neighbour, which should stay at α = 1) reads mean 0.995 before the impact and
**0.96 mean / 0.89 min with ≈ 100 % of bulk cells below 0.98** afterwards, on both APIs, and does
not recover. Consistent with the `saturate` clip turning residual divergence into one-way loss,
which the volume correction then refills in the bulk instead of at the surface. Details and the
new acceptance line are in W-03.

The backyard self-test now has **8 gates**; the no-ball flag is `--backyard-no-ball`. W-20 is not a
bug: `config.json` is read only with `--config`, so **always pass `--scene 7`** and check the log
contains `[Backyard]` lines.

### Stage 3 — W-09, W-10

W-09 done (3a): per-column floor depth, guarded refraction, blended Fresnel. W-10 done (3b): a
`WaterDepth` target and a "Water Depth" pass drawing the water with `FORWARD_PASS` (a forward-only
subset can never draw in RADIAL_DEPTH/GBUFFER/SHADOW_MAP). Measured: the depth dump holds 121,350
pixels confined to the pool; cost +0.179 ms/frame (~3.2%). Remaining: nothing consumes the depth
yet — DOF is force-disabled in this scene and the CoC target dumps all-zero, so the consumer could
not be verified and was deliberately not shipped.

### Review of Stage 3 (2026-10-29) — by the reviewer

Re-run independently at `96d84638`: `--backyard-selftest` PASS on D3D12 and Vulkan; scene 7 0 errors
on all five APIs; floor texture logs depth 0.300…1.560 m.

1. **W-09 accepted.** Checked: `MaxPrimitiveTextures` 24 → 28 does not disturb skinning (bones at
   t24) or lightmaps (t25) — they bind outside the `Textures[]` array, and `RenderQuad` skips
   unnamed slots; `SceneIsNearerAt` derives the depth direction, which is correct under the
   engine's reverse-Z (depth clears to 0); the redeclared `fluidCellSize` is in its own scope.
2. `47500166` — **W-10 redesigned.** The "Water Depth" pass duplicated `Deferred:DEPTH` (see the
   correction under 3b). Pass, target and the `mesh_indices: [5]` check removed; CoC (HLSL, GLSL,
   WGSL) now takes the nearer of slots 0 and 1. All three graphs that run CoC bind slot 1 — WebGPU
   rejects a declared-but-unbound texture and Vulkan's dummy is white, so "leave it unbound" is not
   an option. With DOF on: CoC-near changes in 30,870 texels exactly over the pool; DayScene and
   Minecraft (DOF on by default) 0 errors on all five APIs.
3. **W-21 found** (new task, Stage 3c): `--fluid-selftest` under `--scene 7` fails on Vulkan every
   time (mass drift −18 % … −100 %); under scenes 0, 1 and 6 it passes. Reproduced with Stage 3
   reverted, so it predates this stage. Remaining-work 4.2 now runs the fluid self-test with
   `--scene 0`.

**Lesson for every later stage:** before adding a render pass, dump what the existing targets
already hold. A forward pass that writes depth into its own target is a free depth buffer.

### Stage 3c — W-21

Done. **Root cause: uninitialised GPU volume memory, not frame ownership.**

The Stage 3 review's three diagnostics pointed at per-frame Vulkan state, and both consulting
agents ranked "the scene left a frame open" first. Both were wrong, and the diagnostics are what
showed it:

1. **Debug log, scene 7, Vulkan (failing):** no `Recycling the compute-owned frame` line anywhere —
   the descriptor-pool recycle never fires, so pool exhaustion is out. A temporary
   `RecycleOverflowingComputeFrame` diagnostic that logs when a compute dispatch runs inside a
   renderer-opened frame also **never fired**, so every dispatch was inside a compute-owned frame.
   Frame ownership is therefore not the cause either.
2. **Drain the scene's frame before the self-test** (`CompleteFrame(SubmitNoPresent)` +
   `WaitForGPU()` in `App.cpp`): **3 FAIL / 2 PASS in 5 runs**. Inconclusive, and it exposed the
   key fact the catalogue's table had hidden — **the failure is intermittent**, not 8/8. Two
   discrete outcomes: PASS at −0.00 % drift, or total loss (`7168 → 6`, −99.92 %).
3. **The decisive observation:** `seed` reads back perfectly (8192/8192 samples, `meanX=+1.0`) and
   the *first* step batch then destroys the field, while `--backyard-selftest` (which calls
   `ResetToRest()` before stepping) always passed — and the Backyard self-test's own comment
   already documented the reason: *"Seeding alpha alone leaves freshly created storage textures
   holding whatever was in GPU memory"*.

**The fix.** `FluidSimulation::TryInitialize` now calls `ResetToRest(driver)` once the solver is
ready, so every caller starts from a defined state: velocity, pressure, divergence and both alpha
volumes cleared by `CS_FluidClear3D`. Under scene 0/1/6 the allocator happens to hand back zeroed
memory so nothing showed; after the Backyard solver has created and seeded its own 100×22×50
volumes, the allocator hands back *its* contents, and a caller that only seeds alpha steps from
garbage momentum — hence divergence ×540 after one pressure iteration.

Fixed in the solver, **not** in the harness: any caller that creates a solver and steps it must get
a defined initial state. The temporary `App.cpp` drain and the driver diagnostic are reverted.

**Measured.**

| Configuration | Runs | Result |
|---|---|---|
| scene 7, Vulkan, before | 10 | 3 PASS / 7 FAIL; failures at −99.92 % or −18.21 % |
| scene 7, Vulkan, after | 6 | **6 PASS**, drift −0.00 % |
| scene 7, Vulkan, after, with the harness drain still in | 6 | 6 PASS (so the fix is not the drain) |
| scene 7, Vulkan, after, drain reverted | 6 | 6 PASS |
| scenes 0, 1, 6, 7 × Vulkan and D3D12, after | 8 | **8 PASS**, `meanSpeed=0.1086` (Vulkan) / `0.1083` (D3D12) |

Also verified: `--backyard-selftest` PASS on D3D12 and Vulkan with numbers identical to the Stage 2
baseline; scenes 0, 1, 6, 7 on D3D12, Vulkan, D3D11, GL and WebGPU → **0 errors in all 20
combinations**. Build needed a PCH delete (`Lib\VStuff\Framework\Release\x64\Framework.pch`) after
the headers changed — the documented C2011/C2375 trap, and this time it produced a *stale binary
that silently kept the old behaviour*, so the fix looked 50/50 until the rebuild was clean.

**Lesson for the catalogue.** An intermittent failure with two discrete outcomes is the signature of
reading uninitialised memory, not of a deterministic state bug. The earlier table's "8/8 FAIL" was a
sampling artefact.

### Stage 4 — W-11

Done. Three solver literals promoted (`alphaCompression` 0.5, `maxSubstepDt` 1/120, and the
volume-check interval 15 which had no setter at all), wired through `MakeFluidSettings`, pushed live
in `OnUpdate`, four GUI sliders. The tunable surface is authored in `Scenes/BackyardWater.json` and
loaded by a dedicated leaf TU (`BackyardWaterJson.cpp`) that owns the `glz::meta<WaterTunables>` —
glaze must not reach `DayScene.cpp` through a header, which is the actual MSVC limit. Six keys are
deliberately not settable (grid dims, waterPlaneY, freeboard, floorTextureBound), each documented in
the meta. Strictness is hand-validated against `glz::reflect::keys` because glaze's perfect hash
fails at 101 keys. Measured: shipped file sets 19/101 keys; `pressureIterations` 60 → 20 changes the
log line and the surface probe; an injected typo errors with the key name and exit code 1; the
missing-file path falls back to defaults; all 101 keys round-trip (`101 of 101 keys set`). Gates PASS
on both APIs and all 20 scene/API combinations log 0 errors. My earlier "blocked, field count"
diagnosis in this document was wrong and is corrected in the section above.

### Review of Stage 3c and Stage 4 (2026-10-29) — by the reviewer

1. **Stage 3c accepted.** The diagnosis (an intermittent failure with two discrete outcomes means
   uninitialised memory) and the fix in the solver rather than the harness are right.
2. **Stage 4 accepted**, including the separate `Scenes/BackyardWater.json` and the hand-rolled
   key validation. Checked that the 19 shipped values equal the C++ defaults and that the three
   promoted values equal the old literals, so the run is unchanged.
3. `e974f4e5` — **Android build fixed.** `BackyardScene.cpp` calls `LoadBackyardWaterTunables`,
   defined only in `BackyardWaterJson.cpp`, which was missing from `cmake/AndroidBuild.cmake`. The
   registration validator does not cover Backyard files, so it still printed PASS. Also indented
   the three new GUI slider lines.
4. **Reported Vulkan slowdown: not from these stages.** HEAD measures D3D12 216 / Vulkan 176 fps, and
   Vulkan is 171–183 fps at every earlier commit tested, down to `3a371903`, which measured 313 on
   10-28. Environmental and still unexplained; the evidence and what was ruled out are in
   remaining-work W-15.

### Stage 4a — W-24

Done. Root cause was one sign, as the brief said: both roof slabs and all 26 standing seams used
`rot=(sign * angle, 0, 0)`. A positive X rotation lifts a point at +y, so each slab lifted its
**outer** edge and the roof was a V with the ridge cap floating over the valley. Now
`rot=(-sign * angle, 0, 0)` for both slabs and both seam sets, so the ridge-side edge rises.

Two gable prisms were added (`House_Gable_Left` / `House_Gable_Right`) because the corrected roof
exposes open triangles at `x = min_x` and `x = max_x` — the walls stop at the eave. Each is a closed
triangular prism (`from_pydata`, 6 verts, 5 faces, outward winding) with its outer face flush with
the wall face so the 0.4 m roof overhang covers its top edge. `from_pydata` rather than bpy.ops
because a triangle cannot come from the cube primitive.

**Measured.** The generator now prints the proof the acceptance asks for:

```
[house] House_Roof_Front world Z 3.152 .. 5.148 m (eave 3.2, ridge 5.1)
[house] House_Roof_Back  world Z 3.152 .. 5.148 m (eave 3.2, ridge 5.1)
```

Those are the slab's own 0.10 m thickness either side of the mid-plane, which spans exactly 3.20
(eave, with the 0.5 m overhang) to 5.10 (ridge). `Backyard_House.glb` went from **78 to 80 meshes**
(the two gables), `git diff backyard_layout.json` is **empty**, so the house extents and the pool
layout the solver reads are untouched. The frame-300 back-buffer dump shows a pitched roof with the
ridge highest at the centre and the eaves on the walls. Scene 7 logs 0 errors on D3D12, Vulkan,
D3D11, GL and WebGPU; both self-tests PASS on both APIs.

**Note for future asset runs:** `blender-launcher.exe` returns immediately and swallows console
output. Call `blender.exe` directly to see the `[house]` proof lines.

### Stage 4b — W-23

Done. Assets only — the engine already loaded glTF PBR textures, so nothing engine-side changed.

**Fetching.** New [fetch_backyard_textures.py](../../T850/scripts/textures/fetch_backyard_textures.py) with
a pinned table `{material: (ambientCG id, zip sha256)}`, downloading `_1K-JPG.zip` archives into the
uncommitted `T850/build/texture-cache/`, verifying each hash and extracting **Color, NormalGL,
Roughness and AmbientOcclusion when present**. NormalGL only: the archives also ship NormalDX, which
would invert every bump. Six surfaces: `Plaster001` (house walls), `CorrugatedSteel005` (roof),
`Concrete034` (patio), `Gravel043` (ground), `PavingStones150` (pool coping), `Plaster007` (yard wall).

Gotcha worth recording: **ambientCG answers 403 to clients with no User-Agent**, which is what
`urllib` sends by default. The script sets one. All six hashes are now pinned, so a changed upstream
file fails loudly instead of silently altering the look.

**Generator.** `material()` takes the texture entry and wires Image Texture → Base Color (through a
Mix/RGBA multiply tint where the scan is neutral grey), Image Texture → Roughness, and Image Texture →
Normal Map → Normal. `uv_world_box()` replaces `smart_project` for textured parts: per face, the
dominant world-normal axis picks the two world coordinates, divided by `tile_m`. That makes UVs
metric (a 1 m feature reads as 1 m on every box, whatever its size) — `smart_project`'s scale is
arbitrary per object and would tile differently on each box. Untextured parts keep `smart_project`.
Export now passes `export_tangents=True` and `export_image_format="JPEG"`; the water is exported with
`tangents=False` (no normal map, and tangents would only bloat its 41k-vertex mesh) and its UVs are
untouched — they are the solver mapping.

**Measured**

| Check | Result |
|---|---|
| `git diff backyard_layout.json` | **empty** — textures moved no geometry |
| GLB sizes | house 0.12 → **5.17 MB**, ground 5 KB → 5.13 MB, wall 6 KB → 3.07 MB, pool 24 KB → 1.80 MB (JPEGs embedded); water unchanged at 1.80 MB |
| glTF material check | `House_Stucco`: `baseColorFactor (1, 0.88, 0.74)` + baseColorTexture + normalTexture + metallicRoughnessTexture |
| KeyFlags | 77 of 128 material instances now read `baseColorMap=1 normalMap=1 tangents=1 metallicMap=1`; the other 51 are deliberately untextured (trim, glass, pergola) |
| Frame-300 dump | plaster, corrugated seams, concrete patio, gravel ground, paving coping all visible at believable scale |
| fps, same-session A/B, 3 runs × 1200 frames at 720p D3D12 | **187.5 → 177.2 fps (−10.3 fps, −5.5 %)** |
| Scenes 7, 1, 6 × 5 APIs | **15/15 zero errors** (GL and WebGPU decode the embedded JPEGs) |
| `--backyard-selftest`, `--fluid-selftest` | PASS on D3D12 and Vulkan |

**Known limitation:** occlusion maps are not wired (no `occlusionTexture` in the export). The
archives with AO provide one, but the exporter needs the "glTF Material Output" group; that is the
next step if ambient occlusion is wanted. Not hidden — reported.

**Note:** the house walls read grey-ish under the dusk lighting because the plaster scan is neutral;
the warm tint restores most of the sand colour but not all of it. A warmer plaster asset (e.g.
`PaintedPlaster017`) is the alternative if the owner prefers the reference photo's tone.

### Stage 4c — W-22

Done. The solver now holds a body list instead of one sphere, and the GUI has **Spawn ball** /
**Clear spawned balls** buttons.

**Design, as the catalogue specified.** Body *k* is stamped into the wall mask as `2u + k`; static
solids stay `1u`. Every existing mask reader only tests `!= 0`, so nothing else changed.
`ObjectConstants` (b1) became `uint BodyCount; uint3 pad; float4 BodyVelocity[8]` = **36 DWORDs**
(was 12), and both kernels now recover the body index with `mask - 2u` instead of testing a centre
and radius. The registry's `constant_count` for register b1 in `CS_FluidGradient3D` and
`CS_FluidObstacleVelocity3D` went 12 → 36 in the same commit — R2 again: with the struct grown and
the registry left at 12 the pipelines would fail to create silently. `kMaxBodies = 8` keeps the
gradient root signature at 62 of 64 DWORDs.

**Host.** `std::vector<FluidSphere> m_bodies` (always at least one, so `DroppedSphere()` stays valid);
`AddBody` / `RemoveBodiesFrom` (clamped so body 0, the scripted ball, can never be removed) /
`BodyCount` / `Bodies`. `AdvanceSphere` loops and then resolves sphere–sphere contacts — 28 pairs,
fixed `i < j` order, inverse-mass-weighted positional correction plus a normal impulse with the mean
restitution, two passes for chains. Stamping loops over every body and stamps `2u + k`; the
unchanged-footprint check now compares **each** body's cell AABB and the active count, because two
bodies moving in opposite directions can leave a union AABB unchanged. Substeps take the fastest
body; the volume correction sums every submerged body.

**Scene.** `m_ballPrimitive` is captured at asset load; each spawned ball shares it and gets its own
`PrimitiveInst`, welded to its body every frame. Placement comes from the solver's basin bounds (never
literals) with one cell of margin, radius and density drawn independently, the radius clamped to
≥ 1.2 × `cellSize`, a straight drop from `waterPlaneY + r + 0.5…2.0 m`, up to 32 rejection tries
against existing bodies. Eight new `WaterTunables` keys (`spawnMaxBalls`, `spawnRadiusMin/Max`,
`spawnDensityMin/Max`, `spawnHeightMin/Max`, `spawnSeed`) are in the `glz::meta` (key count 101 → 109)
and shipped in `BackyardScene`'s water JSON. `Reset simulation` clears the spawned balls. New test
hook `--backyard-spawn N` drops N balls at startup so the multi-body path can be exercised without
the GUI.

**New gate 9 — the acceptance GLM asked for.** Two bodies with *different* velocities, one step:

```
[BackyardSelfTest] multi-body  body0 cells=47 maskErrors=0 worst=0.00024 m/s |
                     body1 cells=21 worst=0.00015 m/s (mask 2 and 3)
```

That is the exact failure a single-ball run cannot see: a kernel still comparing `mask == 1u`, or an
off-by-one index handing one ball the other's velocity. PASS on D3D12 and Vulkan.

**Measured**

| Check | Result |
|---|---|
| Spawn 7 with the cap at 7 | 8 bodies, all positions inside the basin, radii 0.13–0.25 m, densities 0.36–1.54 | 
| `spawnSeed` reproducibility | seed 12345, two runs → byte-identical positions, radii, densities, masses |
| fps, same session, 1200 frames at 720p D3D12 | 0 extra **159.4** fps, 1 extra **152.7**, 7 extra **143.7** (−9.9 % for 7) |
| Scenes 0/1/6 and 7-with-2-spawned × 5 APIs | **20/20 zero errors** |
| Frame-300 dump | four spawned balls visible at different sizes and depths, each with its own ripple |
| `--backyard-selftest` (8 gates + gate 9) | PASS on D3D12 and Vulkan |

The fps delta with 7 balls is W-05's motivation: every footprint change re-uploads the whole mask.

**Note discovered while testing:** `T850/bin/x64/Release/Scenes` is a **junction** to
`T850/Assets/Scenes`, so the runtime reads the source tree directly. Editing either path edits the
same file — worth knowing before "syncing" a JSON twice.

**Design notes from the GLM/Qwen consultation (2026-10-29), to apply when the stage runs:**

* **The mask encoding is sound only if every reader is audited for equality, not truthiness.** Static
  solids stay `1u`, body `k` becomes `2u + k`. Any kernel that compares `mask == 1u` breaks
  (moving bodies would fail a "static wall" test); any kernel that *writes* the mask must preserve the
  encoding or bodies silently turn into static walls. Guard the lookup with `mask >= 2u + N` for
  stale masks after a body is removed — an out-of-bounds root-constant/SRV read is undefined on GPU.
* **A face between two body cells is ambiguous** (which body's velocity?) and never occurred with one
  body. It must be skipped as zero-flux: there is no fluid there.
* **Adjacent bodies create a thin fluid gap** with extreme pressure gradients — a resolution limit,
  not a logic bug. Spawn with a minimum gap of one cell rather than changing the solver.
* **Sphere–sphere contact:** 28 pairs, fixed `i < j` order, inverse-mass-weighted positional
  correction plus a normal impulse, 2–4 iterations for chains. Deterministic order and a PRNG seeded
  from `spawnSeed` are what make it reproducible; keep contact resolution entirely on the CPU.
* **Substeps:** max over bodies is correct, but the count jumps when a body enters or leaves. If that
  stutters, clamp it.
* **Per-substep order matters:** integrate bodies → resolve contacts → stamp the mask → upload →
  measure/correct volume → solve. Stamping after the pressure solve uses stale geometry; measuring
  before the stamp lags the correction by a substep.
* **The acceptance test must be multi-body.** Gate 7/8 passing with one ball does not exercise this.
  Spawn one nearly stationary and one fast ball: (1) the fast ball's footprint faces carry *its*
  velocity (no slip-through), (2) the stationary ball behaves exactly like the pre-change single-body
  path, (3) mask values around both read `2 + k`. Qwen adds: no sphere centre may violate the wall
  by more than one voxel over a long run.

### Water mirrored in Z (reviewer fix `9f6e7a2f`, 2026-10-30)

**Symptom.** Spawned balls and their ripples/footprints were on different sides of the pool, as if
flipped on one axis.

**Cause.** The water mesh's UV `v` ran opposite to world Z. `build_water_surface()` authored
`v = j/nz`, and the Blender glTF exporter writes `V = 1 − v`, so the GLB had `v = 0` at z = −4.5 and
`v = 1` at z = −9.0 (checked by reading the file). The solver's surface texture has row 0 at
`basinMinZ` = −9.0. Every consumer that maps mesh UV to solver rows — vertex displacement in
`VS_Mesh`, the normal/foam/floor-depth lookups in `FS_Mesh`, the debug wireframe — drew the solver
mirrored about z = −6.75. The solver and the rendered balls use world coordinates and were right.
The scripted ball drops at z = −6.75, exactly on the mirror line, so the bug was invisible until
W-22 spawned balls off-centre. Because the debug wireframe reads the same UVs, it looked consistent
with the water and could not reveal it.

**Fix.** Author `v = 1 − j/nz` in the generator (so exported V grows with +Z); regenerate.
`BackyardScene::WaterUVMatchesSolver` checks the orientation when the water binds to the solver and
logs an error naming the generator for a mirrored mesh (proved to fire on the old GLB).

**Measured.** Seed 12345, 4 spawned balls, frame 600, D3D12: before, ball 2 (z = −8.49, near the
camera) had its ring drawn near z = −5.0 by the steps; after, every ring is centred on its ball.
Self-tests PASS on D3D12 and Vulkan (fluid meanSpeed 0.1083 / 0.1086, unchanged); scene 7 0 errors on
five APIs; `backyard_layout.json` unchanged. Visual side effect: the tiling normal-map drift now runs
the other way along Z in world space, because it is UV-driven; retune `normalDriftZ` /
`normalDrift2Z` only if it looks wrong against the wind.

### No ball cap (reviewer change `97009aef`, 2026-10-30)

The owner asked for any number of balls. The 8-body cap came from D3D12 root constants (64 DWORDs
per root signature). Per-body velocities now reach `CS_FluidGradient3D` and
`CS_FluidObstacleVelocity3D` as `StructuredBuffer<float4> BodyVelocity` (t9), uploaded per dispatch
through a new engine feature: a `ReadOnlyBuffer` compute binding with inline data (D3D12 upload
ring as a root SRV, Vulkan CB ring, D3D11 immutable structured buffer). `ObjectConstants` is a
4-DWORD header. `kMaxBodies` and `spawnMaxBalls` are gone. Contacts keep the exact all-pairs loop up
to 32 bodies and use a deterministic uniform-grid broad phase above that. A crowded spawn stacks the
ball above whatever it would touch instead of failing. New **Spawn 10 balls** button.

Measured: seeded 4-ball frame 600 byte-identical to the previous build; 100 balls 0 errors on
D3D12, Vulkan and D3D11, all in the pool; self-tests unchanged (gate 9 included); scene 7 0 errors
on five APIs. fps at 720p, same session: D3D12 220 / 199 / 141 and Vulkan 315 / 300 / 222 for 0 / 7 /
100 balls. The slope is per-ball draws plus the whole-mask re-upload on footprint changes (W-05).

**Rule for later stages.** For small per-dispatch arrays (body lists, per-object parameters) use
the inline `ReadOnlyBuffer` binding, not a bigger root-constant block: D3D12 shares 64 DWORDs across
the whole root signature. Test spawn changes with `--backyard-spawn 100` as well as a few balls.

### Independent verification after the two review fixes (2026-10-30, local agent)

Re-checked both commits from the tip (`37682bb6`) rather than trusting the write-ups above.

- **Build.** Green after deleting `Lib\VStuff\Framework\Release\x64\*.pch` (the inline-binding change
  touched `BaseDriver.h`). One transient `D8040` compiler crash on the retry; the second retry was
  green.
- **`9f6e7a2f` (water mirrored in Z).** Read the regenerated `Backyard_Water.glb` directly: `v = 0`
  at engine z = −9.0 and `v = 1` at z = −4.5, which is the solver's row order. No
  "Water mesh UV is mirrored in Z" line at startup. The earlier measurement had already shown the
  *balls* were in the right place, so only the water could have been wrong.
- **`97009aef` (no ball cap).** `--backyard-spawn 100` on D3D12, Vulkan and D3D11: 100
  `[Backyard] Spawned ball …` lines, 101 bodies, every ball inside the basin, 0 `[ERROR]` lines.
- **Gates.** `--fluid-selftest` and `--backyard-selftest` PASS on D3D12 and Vulkan with unchanged
  numbers, gate 9 included. Scenes 0/1/6 × five APIs and scene 7 × five APIs: 0 `[ERROR]` lines.

**New rules learned here.**

- **Check the shader before designing a texture path.** `METALLIC_MAP` samples a metallic-roughness
  texture (`metallic = PBRParams.x * .b`, `roughness = PBRParams.y * .g`), `GLOSS_MAP` reads `.r` and
  ignores the factor, and `OCCLUSION_MAP` is gated on a per-subset material flag — three facts that
  decide how Stage 4d has to be built.
- **A GLB written by hand may carry no UVs at all.** `Backyard_Ball.glb` has only `POSITION` and
  `NORMAL`, so a texture silently cannot bind; check `attributes` before assuming a mesh is
  texturable.

### Stage 4d, part 1 — the ball palette is in the cloud (2026-10-30)

The owner asked for the textures to live in Cloudflare R2 and for the cloud manifest to stay in
sync with the local side. What the buckets hold, measured today:

- `t850-textures`: 43 objects (root `manifest.json` + 42 textures, 481.7 MB).
- `t850-models`: 74 objects (root `manifest.json`, `runtime_assets.json` + 72 models).
- The **same** `manifest.json` bytes are uploaded to both buckets (one union manifest, 114 assets,
  each entry naming its bucket). There is no committed local copy of it; the committed
  `Assets/model-cloud-manifest.json` is the byte-identical twin of `runtime_assets.json`, which is
  the models-only subset the launchers read.
- The token in `C:\Users\camus\Documents\cloud.txt` is an account-scoped API token: bucket listing,
  object read, `PUT` and `DELETE` on `/accounts/{id}/r2/buckets/{bucket}/objects/{key}` all work.
  It is never copied into the repository. `web/cloudflare.local.json` stays absent, so the npm
  tooling keeps using `CLOUDFLARE_API_TOKEN`.

New tooling, both authoring-only:

- `scripts/textures/fetch_ball_materials.py` packs the 32 CC0 sets at 512² into
  `Assets/Textures/Ball/<id>/{Color.jpg, Normal.png, ORM.png}` (96 files, 16.78 MB) from pinned
  ambientCG archives. A corrupted archive aborts with a sha256 mismatch (negative-tested by
  flipping a byte in `Metal009_1K-JPG.zip`).
- `scripts/PushCloudTextures.ps1` is the first publish tool in the repo: dry run by default,
  `-Apply` to upload, `-Prune` opt-in. It treats the cloud manifest as the source of truth, aborts if
  the two bucket copies have drifted, changes only the entries for the scanned prefix, uploads
  objects before the manifest (and deletes before republishing the pruned manifest), verifies every
  upload by reading it back and comparing SHA-256, publishes to both buckets and re-verifies both,
  and adds `sha256` to **new** entries only (older entries stay size-only). A PowerShell JSON round
  trip of the current manifest is byte-identical, so publishing does not churn untouched entries;
  the only new top-level field is `manifestVersion`.

Measured run: 96 objects uploaded (16.78 MB), manifest 114 → **210 entries, 42 → 138 textures**,
`manifestVersion` 1, 110,459 bytes; both buckets verified identical; a re-run reports "already
current" and publishes nothing. `DownloadTextures.ps1` reports 138/138 ready, and deleting
`Ball/Marble016/ORM.png` made it re-download the file and verify the manifest's sha256.

**Rules learned here.**

- **Publish objects before the manifest, and verify the object rather than the status code.** The
  `ETag` returned to `Invoke-WebRequest` on a `PUT` is not reliable; reading the object back through
  the authenticated API and comparing SHA-256 is.
- **`r2.dev` propagates, it does not race.** A freshly stored key can take a minute or two to appear
  on the public hostname, so a per-object public `HEAD` immediately after upload is not a valid
  failure signal; verify through the API, then sample the public hostname at the end.
- **Never skip an upload on size alone.** New entries carry a `sha256`, so re-runs are genuinely
  idempotent; entries without one are compared by size and can only be forced with `-Force`.

### Stage 4d, part 2 — a random material on every ball (2026-10-30, reviewer, `e055efec`)

**Asset.** `scripts/models/build_ball.py` now writes a spherified cube (16×16 quads per face, 1734
vertices, 3072 triangles) instead of a UV sphere. The balls never rotate and the camera looks down,
so a UV sphere's pinched pole would sit exactly where the viewer looks. Each face has a full 0..1 UV
square and the four side faces join seamlessly for tiling textures. The material declares
`baseColorTexture`, `normalTexture` and `metallicRoughnessTexture` as embedded 1×1 PNGs that
reproduce the old red rubber look, with every factor 1.0. Declaring the maps is the point: the glTF
loader then sets `DIFFUSE_MAP | NORMAL_MAP | METALLIC_MAP` and generates MikkTSpace tangents
(`GLTFMesh.cpp`, UV0 + normals present), so no tangent attribute and no key surgery are needed.
The JSON chunk is now space-padded as the glTF spec requires (it was NUL-padded; the engine
tolerated it, Python's `json` did not).

**Renderer.** `RenderMesh::Draw` bound material textures first and only fell back to the instance
(normal slot only), so a per-instance swap did nothing. `PrimitiveInst::InstanceMaterialMaps` (pushed
into `PrimitiveBase::m_instanceMaterialMaps` for one draw, then restored, like the textures) makes
instance slots 0, 3 and 6 win. It is a plain flag, not a `ShaderKey` bit, so it creates no
permutation and needs no precompile. Scenes that never set it are unchanged.

**Scene.** `LoadBackyardBallPalette` lives in `BackyardWaterJson.cpp` (the existing glaze leaf TU,
so no vcxproj/CMake/Android registration). Ids and map names must be plain names, because they
become path segments. Sets whose files are not on disk are dropped once at load with a message
naming `DownloadTextures.ps1`. A set's three textures are created on first use and destroyed in
`DestroyAssets`. The pick is a shuffle bag (every material once before a repeat) driven by its own
LCG seeded from the spawn seed, so `spawnSeed` still reproduces positions exactly. The scripted ball
also draws from the bag, and each spawn log line names its material.

**Verified.**

- `--backyard-spawn 32`, D3D12: 0 errors; 31 spawned + the scripted ball = 32 distinct materials;
  the frame dump shows chain mail, polished silver reflecting the sky, copper, marble, rock and cork,
  with no visible pole.
- `--backyard-spawn 100` on Vulkan and D3D11: 0 errors, all 32 materials used.
- Self-tests PASS on D3D12 and Vulkan; gate 9 is unchanged (`body0 cells=47 maskErrors=0
  worst=0.00024`). Scenes 7, 1 and 6 on five APIs: 0 errors. GL and WebGPU load the palette (the
  scripted ball wears it; there is no solver to spawn with). The full x64 solution, including
  T8ditor, builds.
- fps, D3D12 720p, same session: 100 balls with materials **140.6** against **147.2** with the
  placeholders (palette file hidden). The 600-frame windows overlap (132–148 against 131–148), and
  the recorded 100-ball baseline was 141. 0 balls: 215.5.

**Not done.** Gate 10 and `probe_ball_materials.py` from the plan. No per-ball AO (subset-gated),
no per-ball tint. A fresh clone without `DownloadTextures.ps1` shows the red placeholder balls and
logs `Ball palette: only 0 of 32 materials are on disk`.

**Rules learned here.**

- **Let the asset declare the maps and swap textures per instance; do not inject map bits.** The
  loader only builds a textured permutation, and tangents, for maps the material declares.
- **Material textures beat instance textures in `RenderMesh::Draw`.** A per-instance override needs
  the explicit opt-in.
- **A second RNG for cosmetic picks keeps seeded physics reproducible.**

### Pick and drag, PBR fixes, unattended runs (2026-10-30, reviewer)

**Wind** acts on the water only: `windX/Y/Z` feed the solver kernels' constants, the ball integrator
has no wind term, and the water applies no force back on a ball. Wind cannot move the balls.

**PBR fixes (`c4c680dc`, HLSL, GLSL and WGSL alike).**

- The deferred passes multiplied the sun's shadow-map term into every light, so point lights went
  dark inside the sun's shadow; it now gates directional light only.
- SSAO was multiplied into direct light; it now scales indirect (IBL/ambient) light only.
- `ShadowAccum` is `RGBA8` in every render graph: R = sun shadow, G = SSAO (it was their product
  in one `R8` channel). Dumps on all five APIs show R with hard 0 minima and G with soft minima.
- Generated tangents carry glTF handedness. glTF V runs down the image, so MikkTSpace fed raw
  glTF UVs returned the opposite sign, and normal-map bumps were inverted on meshes without
  `TANGENT` (the ball). The naive fallback got the same sign rule.

**Pick and drag (`c920c9a5`).** Hold **Ctrl**: the scene stops capturing the mouse (its
`AllowsMouseCapture` override), so the cursor is free even in the first-person camera. Ctrl + left
click on a ball holds it under the cursor; the wheel moves it nearer or farther; releasing the
button throws it with its last velocity. The cursor stays free while a ball is held. A click
without Ctrl is still the camera's. A held body (`FluidSphere::held`) skips gravity, buoyancy and
drag, and contacts treat it as infinitely heavy, so it shoves other balls.

**`--backyard-pick-test`** feeds a synthetic `InputManager` to the scene's own input path, so no OS
input is generated. Gates: a no-Ctrl click does not pick; Ctrl + click picks the ball under the
cursor; capture is freed; the dragged ball follows the cursor ray (0.003 m off) and pushes another
ball; release; re-grab; a final frame dump shows the ball held at a known pixel. Run it with
`backyard-diag\pick_test.ps1` (`--unattended --regressionFixedDt 0.0033 --backyard-spawn 6
--backyard-pick-test`). PASS on D3D12, Vulkan and D3D11; GL and WebGPU SKIP (no solver). The D3D12
and Vulkan dumps show the held ball centred on the synthetic cursor.

**`--unattended` (`210cb571`)** disables mouse capture and creates the window without activating
it. That exposed a latent bug: the D3D11 and D3D12 drivers took `GetActiveWindow()` instead of the
window they were given, which is null for a window that never got focus
(`CreateSwapChain` failed with `DXGI_ERROR_INVALID_CALL`). Both now read the HWND from the SDL
window. The 19-run matrix is 0 errors with the flag on every run.

**Rules learned here.**

- **Never drive the owner's real input from a test.** Use `--unattended` and in-process input.
- **`GetActiveWindow()` is not "my window".** Take the handle from the window you created.

### Performance pass and documentation audit (2026-10-30, reviewer)

**`c911381a`: three stalls removed.** Numbers, method and the remaining cost breakdown are in
remaining-work W-15, "Profiling pass 2026-10-30". In short:

- **D3D12 float uploads wait no more.** `D3D12Texture::UpdateFloatData` stages the data in the
  frame's upload ring (`D3D12Driver::AllocateRingUpload`) and records the copy in the frame's
  command list; outside a frame it keeps the old blocking path. The point-light pass uploads two
  such textures per frame, so this was 3.1 ms of every D3D12 frame in every scene with point lights.
- **Volume correction without readback.** `CS_FluidVolumeReduce3D` plus a GPU-side deficit in
  `CS_FluidVolumeCorrect3D` (implementation record §10).
- **Wall mask restamped on the GPU.** `CS_FluidStampBodies3D` (implementation record §8.3).

Validation: both self-tests PASS on D3D12 and Vulkan, gate 9 identical, mass drift unchanged, the
19-run matrix 0 errors, and a D3D12 DayScene frame byte-identical between the old and new upload.
Result at 720p with 0 balls, same session: D3D12 222 → 292–305 fps, Vulkan 322 → 344–361 fps,
p99 ≈ 4 ms, 0 spikes.

**Documentation audit.** The engine documents now match the Framework changes since `47500166`:

| Change | Document |
|---|---|
| `ShadowAccum` R = sun shadow, G = SSAO; how deferred lighting applies them | `rendering/cascaded-shadow-maps.md` |
| Inline `ReadOnlyBuffer` data; Vulkan 3D storage (the table said "not yet") | `rendering/compute-shader-implementation.md` |
| `UpdateFloatData` per API, D3D12 ring path | `rendering/textures-and-ibl.md` |
| Generated tangent handedness | `geometry/loading-geometry.md`, `Framework/src/utils/gltf/HANDOFF.md` |
| `PrimitiveInst::InstanceMaterialMaps` | `rendering/geometry-rendering-flow.md` |
| `--unattended`, `App::AllowsMouseCapture`, HWND from the SDL window | `development/runtime-configuration.md`, `architecture/platform-event-loop.md`, `input/camera-and-controls.md`, `debug/visual-regression.md`, `AGENTS.md` |
| Kernels, wall mask, correction, picking, tools | `scenes/backyard-water-simulation.md` |

**Rule.** A Framework change is not done until the engine document that describes it is updated in
the same commit or the next one.

### Stage 4d, part 3 — gate 10 and the frame probe (2026-10-30, local agent)

Stage 4d shipped without its proof. Both halves are now in, and both were negative-tested.

**Gate 10** (`BackyardSelfTest.cpp`, headless, runs with every `--backyard-selftest`). Three facts,
each one a separate failure message:

1. the palette lists at least 32 materials;
2. every set resolves on disk through `ResourceLocator` — the check names the first missing set and
   points at `scripts/DownloadTextures.ps1`;
3. every set **decodes on the running API** — one set at a time, created and destroyed, so the gate
   never holds 96 textures.

It then draws one full cycle from the **real** shuffle bag and asserts it is a permutation. The bag
moved out of `BackyardScene` into `BackyardWaterJson.{h,cpp}` as `BackyardBallMaterialBag` so the
scene and the gate run the same code instead of a copy; the LCG and its seeding are unchanged, so
material sequences are identical to before.

Measured: D3D12 and Vulkan `ball materials palette=32 onDisk=32 decoded=32 bagPermutation=ok`, both
self-tests still PASS with every other gate unchanged. Negative test: moving
`Assets/Textures/Ball/Cork001` aside gives exit 1 and `31 of 32 sets are on disk (first missing: set
32); run scripts/DownloadTextures.ps1`.

**The probe** (`scripts/validation/probe_ball_materials.py`) had to be redesigned once. The first
version projected each spawned ball into a frame dump and sampled its colour; that produced false
failures because two balls can overlap in screen space and two metals can legitimately look alike at
a handful of pixels. Comparing colours *between balls* was the wrong test.

The working test is an **A/B of the same frame**: one run with the palette, one with
`Scenes/BackyardBallMaterials.json` moved aside (the balls fall back to the GLB's red placeholder),
same `--backyard-spawn-seed`, same frame. Each ball's spawn position is projected with the engine's
own `camVP` from `snapshot.json`, and its patch must differ between the two dumps. A log line can
lie; a pixel that changes only because the palette exists cannot.

- Projection convention, measured rather than assumed: `snapshot.json`'s `camVP` is row-major and
  the engine uses row vectors, so `clip_j = Σ v_i · M[i][j]`. (The column-major reading put every
  ball in the sky.)
- The dump is frame 3: the balls have fallen 0.007 s, so they are still on their logged spawn
  positions.
- Measured: D3D12 and Vulkan both PASS with 33 bodies, 32 distinct materials, one expected repeat,
  and a **mean per-ball palette-vs-placeholder difference of 76.6 / 76.7** out of 255.

**New CLI hook.** `--backyard-spawn-seed <n>` overrides `spawnSeed` for a run, so a test can
reproduce a spawn layout without editing `Scenes/BackyardWater.json`.

**Rules learned here.**

- **Prove the rule and prove the pixels separately.** "32 distinct strings in a log" is not evidence
  that the renderer used them, and a colour comparison between balls is not evidence of a rule.
- **An A/B against a documented fallback is a cheap, honest probe.** Hide the input, re-run the same
  seed, and require the output to change where it should.

### Stage 5a — W-02

**Part 1** (`14c705f4`, local agent): the `R32Float` format on five backends and an exact R32F 3D
round trip in the compute self-test. **Part 2** (reviewer): the switch, after the local agent had
measured it, reverted it and handed over three hypotheses.

**The switch.** The five writer kernels (`CS_FluidSeedAlpha3D`, `CS_FluidAlphaAdvect3D`,
`CS_FluidVolumeCorrect3D`, `CS_FluidDivergence3D`, `CS_FluidPressure3D`) write `RWTexture3D<float>`
with `[[spv::format_r32f]]`; their registry bindings declare `r32float`; `CreateVolume` takes a
format; `CS_FluidClearFloat3D` clears the scalar volumes on reset. Readers are unchanged: they read
`.x` through `Texture3D<float4>`, and an R32F view returns `(r, 0, 0, 1)`. That also covers
`CS_FluidCopy3D`, so `ReadVolumeFraction` still returns one float per voxel.

**Why the gates moved, measured** (D3D12; Vulkan and D3D11 agree):

1. **The isotropy probe was off-centre.** `centreZ = uint((ball.z − basinMinZ) / cellSize)`
   evaluates 2.25 / 0.09f = 24.999998 → 24, so the annulus sat one cell toward −Z. The quadrant index
   is `(dx > 0) + 2 (dz > 0)`, so the "axis" value is the −Z half against the +Z half: the gate was
   reading the wave's radial profile at 0.56 m and 0.44 m. R32F's sharper wave turned that into "X up,
   Z down" (29.2 mm); fp16 gave 10.7 mm, which Stage 1b had recorded as operator-split anisotropy.
   Centred on the impact (fractional grid coordinates), fp16 measures mirrorX 0.06 mm / mirrorZ
   0.2 mm and R32F 0.76 / 0.5 mm at +0.30 s. The gate is renamed mirrorZ and shares the 3 mm
   limit. Quadrants span both axes, so it cannot measure X-vs-Z anisotropy at all.
2. **fp16 α diluted the whole pool; R32F leaves air only where a plunge put it.** Column dumps of the
   floating-ball test at 8 s: fp16 carries 0.55 cells of air below the surface band in *every*
   column, so all columns read low by the same amount and the surface looked flat. R32F carries
   0.003 cells far away but 0.10–0.21 cells within 0.3 m of the ball (air the plunge entrained, which
   has no buoyancy to leave). The free surface itself is flat, but the column integral counts that
   air as missing water and drew a 1.7–3 cm moat. `CS_FluidSurface3D` now counts air deeper than two
   cells as volume (ramped, continuous): ring −17.1 → −4.9 mm, covered 19.0 → 15.9 mm, flicker
   0.7 mm. Gate 7's limits are unchanged.
3. **Gate 5's 1 mm floor leaned on fp16 noise.** It catches a surface kernel that copies row 0 into
   every row, which makes the across-Y median exactly 0; R32F's correct kernel measures 3.0 mm / 0.7
   mm. The floor is 0.25 mm with the measurement next to it.
4. **Gate 1 is strict now.** The still pool no longer sinks (mean −0.2 mm, loss 0.001 % on three
   APIs), so its regression band (12 mm, 0.80 %) became 2 mm and 0.05 %, as its comment promised.

The other agent's third hypothesis ("α fp16 + pressure fp32 gives a +43 mm mound") was not pursued:
all three fields switched together pass, and that mixed configuration does not ship.

**The surface kernel was the biggest cost in the frame.** The first trapped-air version cost about
0.5 ms, which exposed that the kernel recomputed every column 9 times (once per tent tap, each with
up to 64 inpainting column walks beside a body). An 8×8 group now integrates its 10×10
neighbourhood once into `groupshared` memory; gate numbers are identical to the 0.1 mm.

| 720p, same session, `telemetry.ps1` | HEAD (fp16) | R32F only | R32F + tiled surface |
|---|---|---|---|
| D3D12, 0 balls: mean fps (p99) | 306.8 (4.62 ms) | 315.8 | **415.3 (3.79 ms)** |
| Vulkan, 0 balls | 353.4 (3.95 ms) | 368.4 | **402.3 (3.70 ms)** |
| D3D12, 30 balls | 266.7 (4.97 ms) | — | **377.0 (3.94 ms)** |
| Vulkan, 30 balls | 327.2 (4.39 ms) | — | **360.2 (3.88 ms)** |

Validation on the final binary: `--backyard-selftest` PASS on D3D12, Vulkan and D3D11 (all 10
gates); `--fluid-selftest` PASS on all three (meanX +0.0066, was +0.0201); `--compute-selftest`
exit 0 on all three; scene 7 with 8 balls on five APIs, 0 errors; material probe PASS.

**Rules learned here.**

- **A gate that moves when accuracy improves is suspect first.** Two of three were measuring the
  test or the old noise, not the solver.
- **Never truncate a physical position to a cell index for a probe.** Use fractional grid
  coordinates; float32 division lands just below integers.
- **Name a gate after what its arithmetic compares.** The "axis" gate compared −Z with +Z for two
  stages while its comment said X vs Z.
- **Profile a fix before calling it done.** The correct surface fix first cost 15 % of the frame.

### Stage 5b — W-03

Attempt 1 (operator splitting) measured and rejected; the stage is re-scoped to a less diffusive flux.
Numbers, tables and the two candidate designs are in section 3, "Stage 5b, attempt 1".

### Stage 6a — W-05, W-04

_Pending._

### Stage 6b — W-07, W-06

_Pending._

### Stage 7 — W-15

_Pending._

### Stage 8 — W-14, W-16, W-17

_Pending._
