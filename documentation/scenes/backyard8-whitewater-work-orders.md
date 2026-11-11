# Backyard 8 whitewater: work orders for the local agents

Branch: `backyard8-whitewater` (checkpoint `3ad6d1a`, on top of `ec0ee11`). Read the
checkpoint's commit message and `git show --stat 3ad6d1a` first: it lists every new kernel
and engine change. Engine rules: [AGENTS.md](../../AGENTS.md) and the skills under
`.github/skills/` (`t850-compute-kernel`, `t850-scene-runtime-controls`,
`t850-visual-regression`, `t850-local-build-validation`, `t850-build-run`).

## Rules for every work order

1. **File ownership is strict.** Edit only the files listed under your work order. Claude is
   tuning in parallel and owns everything else, in particular all `Assets/Shaders/*.hlsl`,
   `Framework/include/simulation/PbfFluid.h`, `Framework/src/simulation/PbfFluid.cpp`,
   `Framework/include/scene/ParticleSystem.h`, `Framework/src/utils/ComputeKernelRegistry.cpp`
   and `Assets/Scenes/Backyard8Scene_RenderGraph.json`. If your task needs a change there,
   write it under "Requests to Claude" at the bottom instead of editing.
2. **Commit only your own paths**, on this branch, one commit per work order:
   `git commit -m "WO-n: ..." -- <your paths>` (a pathspec commit, so nobody else's staged or
   unstaged files are included). Do not push, rebase, amend or switch branches.
3. **Never report a pass you did not run.** Distinguish pass, fail, skip and environment block.
   All DayScene runs use `--unattended`. Linux runs need
   `SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu`.
4. Linux build: `cd T850 && cmake --build build/linux --target DayScene -j$(nproc)`.
5. When done, fill in your row of the status table at the bottom (commit hash, results).

## WO-1: config file and Dev GUI for the new settings (start now)

Files: `T850/DayScene/Backyard8FluidJson.h`, `T850/DayScene/Backyard8Scene.cpp`,
`T850/DayScene/Backyard8Scene.h`, `T850/Assets/Scenes/Backyard8Fluid.json`.

The new settings exist in C++ but cannot be set from `Backyard8Fluid.json` or the Dev GUI yet.

- `screenSpace` keys for the new `ParticleScreenSpaceDesc` fields
  (`Framework/include/scene/ParticleSystem.h`): `envIntensity`, `sunAttenuation`,
  `causticFocus`, `contactLine`, `backlight`, `scatterSun`, `scatterSky`,
  `scatterAnisotropy`, `rippleCalm`, `rippleAgitation`, `foamStrength`,
  `foamTextureScale`, `aeration`, `bubbleStrength`, `sprayStrength`, `wetness`. Map them in
  `Backyard8Scene::BuildParticleDesc` with the same clamps `ParticleSystem.cpp` validates.
- `debugView` now goes to 9 (7 whitewater, 8 light under water, 9 wetness). The clamp in
  `BuildParticleDesc` is still `0..6`: raise it to 9 and add the three names to the debug combo.
- A new top-level `whitewater` section mirroring every field of `t850::PbfWhitewaterSettings`
  (`Framework/include/simulation/PbfFluid.h`). `Backyard8Scene::ConfigureWhitewater()` must
  build the settings from it instead of `PbfWhitewaterSettings{}`.
- JSON values = the current C++ defaults (copy them exactly; the shipped JSON and the C++
  defaults must agree). The schema is strict: unknown keys are rejected, so every key in the
  JSON must exist in the struct.
- Dev GUI (`DrawScreenSpacePanel`): a "Scene lighting" section (env intensity, sun
  attenuation, caustic focus, contact line, backlight, in-scatter sun/sky/anisotropy, ripple
  calm/agitation) and a "Whitewater" section (foam, foam cell size, aeration, bubbles, spray,
  wetness, plus the solver's emission rates, lifetimes and drying time). Follow the existing
  `FluidPanelControls` pattern and `lookChanged`. For the solver fields, call
  `m_pbf.SetWhitewater(...)` and then `RegisterParticleSystem()`: changing `maxDiffuse`,
  `wetResolution` or `wetTexel` recreates the solver buffers, and the descriptor's geometry
  (pool capacity, wetness map size) must be refreshed.

Acceptance: Linux build; a 300-frame run of
`DayScene --unattended --api vulkan --scene 8 --dump-frame 300` logs no `[ERROR]`; a run with
an unknown key in the JSON fails to load the config with a clear error (then remove the key);
`python3 T850/scripts/validation/backyard8_regression.py --skip-captures` still passes both
levels. The rendered image must not change (same values as the C++ defaults).

## WO-2: constant-layout checker for the new kernels (start now)

Files: `T850/scripts/validation/check_fluid_constants.py` only.

Extend the checker so it also covers `CS_Pbf*.hlsl` and `CS_FluidWhitewater.hlsl`, and
cross-checks the C++ mirrors:

- `cbuffer PbfWhitewaterConstants` is shared by `CS_PbfWhitewater`, `CS_PbfDiffuse` and
  `CS_PbfFields`: identical member lists, 48 DWORDs, matching `struct PbfWhitewaterConstants`
  (`static_assert(... == 192)`) in `Framework/src/simulation/PbfFluid.cpp` and the manifest's
  `constant_count` for `pbf_whitewater`, `pbf_diffuse`, `pbf_fields`.
- `CS_ParticleShade`: 100 DWORDs = `ParticleShadeConstants` (400 bytes) in
  `Framework/src/utils/ComputeKernelRegistry.cpp` = manifest `particle_shade`.
- `CS_FluidWhitewater`: 28 DWORDs = `WhitewaterSplatConstants` (112 bytes) = manifest
  `particle_whitewater`.
- Compute the HLSL cbuffer size with the HLSL packing rules (a vector cannot straddle a
  16-byte row; arrays start on a new row).

Acceptance: passes on the branch; demonstrate it FAILS (non-zero exit, clear message) when
you temporarily change one manifest `constant_count` and, separately, one member of one of the
three shared cbuffers. Revert the mutations before committing.

## WO-3: documentation draft (start now; finish after WO-4)

Files: `documentation/scenes/backyard8-ssfr-whitewater.md` (new), `documentation/README.md`
(one row after the `backyard8-ssfr-realism.md` row),
`documentation/scenes/backyard8-screen-space-fluid.md` (pipeline diagram, Tuning, debug views).

Write the record from the design summary below and the checkpoint commit. Leave every number
(cost, coverage, regression results) as `TBD` until WO-4 measures it; do not invent values.
Structure: what changed and why (one row per artifact/improvement), data flow (solver kernels →
published buffers → graph passes), config keys and debug views, cost, verification, remaining
work. Link the earlier records (`backyard8-ssfr-realism.md`).

## WO-4: validation and backends (start ONLY when the status table says READY FOR WO-4)

Files: GL twins `T850/Assets/Shaders/*.glsl`, `T850/scripts/validation/backyard8_references.json`.

1. GL twins for every changed or new kernel: `CS_FluidClear`, `CS_FluidSplat`,
   `CS_FluidWhitewater`, `CS_ParticleShade`, `CS_PbfWhitewater`, `CS_PbfDiffuse`,
   `CS_PbfFields`. Linux:
   `SPIRV_CROSS=$HOME/src/SPIRV-Cross/build/spirv-cross python3 T850/scripts/shaders/generate_gl_compute_twins.py <names>`
   (that build reproduces the committed twins byte for byte). Then `check_gl_twins.py` and
   `check_fluid_constants.py`.
2. Linux: build, `DayScene --unattended --game-selftest` (expect 78 PASS, exit 0), then
   `backyard8_regression.py`. The `splat` and `spheres` captures and both water levels must
   pass WITHOUT re-freezing (that path did not change). Only `screen-space` may be re-frozen,
   and only after Claude has signed off the look.
3. Windows x64 Release, native (not Proton): build, then
   `python scripts\validation\backyard8_regression.py --api <api> --day-scene bin\x64\Release\DayScene.exe`
   for `d3d12`, `d3d11`, `gl`. Report per API and attach one back buffer per API at frame 300
   of the water start. Expected risks: the cube map bound to a compute kernel (D3D11/D3D12
   SRV of a DDS cube, GL `samplerCube`), half-resolution targets, and the 100-DWORD constant
   block (D3D12 root CBV path).
4. Cost: `--profile --profileFrames 400` at the default camera, screen-space mode, water and
   lake start. Report the GPU time of every `Fluid *`, `Whitewater Splat` and solver pass.

## Design summary (for WO-3)

| Item | What | Where |
|---|---|---|
| Off-screen reflections | Rays the screen cannot resolve sample the scene's sky cube (`@environment_map`, compute cube binding added to the engine); on-screen hits keep SSR, frame exits keep the exit-pixel continuation. | `RenderGraph.cpp`, `ComputeKernelRegistry.cpp`, `ComputeReflection.cpp`, D3D11/D3D12/Vulkan/WebGPU compute, `CS_ParticleShade` `EnvRadiance` |
| SSR false hits | A crossing is accepted only if the G-buffer normal there faces the ray. | `TraceReflection` |
| Refraction under the coping | The refracted ray is marched through the depth buffer; a hidden hit takes the analytic pool-box distance and the last visible sample. | `MarchRefraction`, `PoolBoxExit` |
| Pool behind a splash sheet | Water in the air shades the resting pool surface behind it as a second interface. | `PoolSurfaceBehind` |
| Spray classification | Per-particle spray flag from the anisotropy pass instead of the screen-ring heuristic. | `CS_FluidSplat` aux channel 3 |
| Sunlight under water | Direct sun rebuilt from G-buffer albedo, normal and `ShadowAccum`, absorbed along the refracted sun path; caustics only where the sun reaches. | `SubmergedRadiance` |
| Simulated caustics | Focusing 1/(1 + c·d·∇²h) of the smoothed PBF column heights modulates the procedural web. | `CS_PbfFields` mode 1, `CausticFocus` |
| In-scatter | Sun (Henyey-Greenstein, surface shadow) and sky light scattered toward the eye replace the constant fluid colour. | `InScatter`, `SurfaceSunVisibility` |
| Sun glitter | Slope variance of ripples too fine for the pixel widens the glint and reflection cone (Toksvig). | `RippleGradient`, main kernel |
| Flow ripples | Ripple slope follows per-particle agitation (smoothed speed); hashed, irregular wave set. | `CS_PbfWhitewater` z, `RippleGradient` |
| Backlit crests, meniscus | Thin water toward the sun glows; brighter contact line at walls. | main kernel |
| Half resolution | Depth/thickness splat and smoothing at half resolution (`$window_half`), depth-aware upsample in the shade; the splat tests the farthest full-resolution depth of each half pixel. | graph JSON, `CS_FluidSplat`, `SampleFluid` |
| Foam on the water | Ihmsen 2012 potentials (trapped air, crest, energy); per-particle foam and entrained air that decay; foam lace texture; milky aeration. | `CS_PbfWhitewater`, `CS_FluidSplat` aux 0..2, shade |
| Spray, foam, bubbles | Secondary particles: ring buffer, classified by fluid neighbours (spray < 6, foam 6..20, bubble > 20), splatted into per-pixel channels; bubbles dimmed by the water above. | `CS_PbfWhitewater`, `CS_PbfDiffuse`, `CS_FluidWhitewater` |
| Wet deck | Water landing on the deck/coping stamps a world-space wetness map that dries (45 s); wet surfaces are darker with a sky-reflecting film. | `CS_PbfWhitewater`, `CS_PbfDiffuse`, `CS_PbfFields` mode 0, shade |

## Status

| WO | Owner | State | Commit | Results |
|---|---|---|---|---|
| Claude: tuning and visual sign-off | Claude | done (Vulkan) | see `git log` "Backyard 8: tune whitewater" | Potentials calibrated on particle dumps (trapped air 3..15, energy 0.5..10, crest 0.3..2; calm pool never emits); shorter freshwater lifetimes; secondary particles drawn as soft density blobs (no pixel dots); foam lace modulates opacity; splash droplets shaded as whitewater; caustics capped at 2.5x; half-res splat no longer tests opaque depth (the shade re-tests at full res). Cost (GPU, 1080p, Vulkan): Fluid Splat 0.83 ms default camera / 2.4 ms close-up camera (was 1.85 / 7.0 in ec0ee11); shade 0.13-0.23 ms; Whitewater Splat 0.17 ms during the splash. |
| WO-1 config + GUI | qwen-5090 | **done**, checked by the orchestrator | `b060602` | Orchestrator re-ran every check on Linux (Vulkan): build clean (`cmake --build build/linux --target DayScene`, binary rebuilt after the commit). `--unattended --api vulkan --scene 8 --dump-frame 300` → exit 0, **0** `[ERROR]`. Unknown key: `screenSpace.notAKey` → `[Backyard8] Cannot parse ...: 148:6: unknown_key` + `Fluid config rejected: 148:6: unknown_key` (file deleted afterwards). **Debug views 7/8/9 verified by dump statistics** (frame 121, water start, 1280x720): view 6 `434141 px, mean (39.10, 80.28, 97.29)`; view 7 **`364149 px, mean (35.02, 27.17, 29.87)`**; view 8 **`431421 px, mean (55.96, 141.63, 97.83)`**; view 9 **`417604 px, mean (81.24, 119.98, 135.71)`**; shaded view 0 `434047 px, mean (46.83, 83.98, 100.15)`. All five md5s differ, so the 0..6 clamp bug is gone. `backyard8_regression.py --skip-captures` → **RESULT: PASS** (lake −0.0439 m, water −0.1703 m). `--game-selftest` → **78 PASS, exit 0** (extra, not required). No-render-change check: **inconclusive as a byte comparison** — this renderer is not deterministic even with an identical binary and config (two consecutive 1280x720 runs at frame 122: 1,013,866 / 2,764,800 bytes differ, means (45.54, 82.86, 99.17) vs (46.16, 83.52, 99.88)). Substituted a fair test: re-running with a JSON that **omits all 16 new `screenSpace` keys and the `whitewater` block** (so the values come from the C++ defaults) gave `295441 px, mean (45.8, 91.7, 109.6)` vs `295115 px, mean (45.9, 91.9, 109.9)` with the shipped JSON — no systematic shift, i.e. the JSON defaults agree with the C++ defaults. Note: the **`screen-space` captures fail** the full gate (f24 0.84% / f90 52.4% / f300 112.1% pixels), but removing WO-1's keys does not change that, and the references were last frozen at `ec0ee11` while the shade was rewritten in `3ad6d1a` and retuned in `9f83cd1`; `splat` and `spheres` pass at 0.003-0.31%. That is the stale-reference issue WO-4 §2 owns, **not** a WO-1 regression. |
| WO-2 constants checker | qwen-local | **done**, checked by the orchestrator | `968f450` + `68e979b` (WO-2b) | Default invocation `python3 T850/scripts/validation/check_fluid_constants.py` → **exit 0**: `PASS: 15 cbuffer name(s) across 46 fluid kernel(s), 51 registered constant_count(s) match, 3 struct/kernel cross-check(s) (C++ mirror vs HLSL vs manifest)`. Failures re-demonstrated by the orchestrator on the real tree, each reverted byte-identical (`git diff --stat` empty after every revert): (a) manifest `particle_whitewater` 28→32 → `CS_FluidWhitewater.hlsl: cbuffer WhitewaterSplatConstants at b0 is 28 DWORDs but compute_kernels.json (particle_whitewater) declares constant_count 32`, exit 1; (b) `CS_PbfDiffuse.hlsl` `GridZ`→`GridZ2` → `GridZ2 (HLSL) does not match C++ member gridZ at byte 16 / DWORD 4`, exit 1; (c) `CS_PbfFields.hlsl` `float`→`float2 GridOriginX` → 43 problems incl. `CS_PbfFields.hlsl has a different layout (48 members) than CS_PbfDiffuse.hlsl (48)` and `is 52 DWORDs but ... declares constant_count 48`, exit 1. Grouping fix proven real, not a blanket suppression: reassigning `pbf_particles` `constants_builder` to `particle_splat` in a `/tmp` copy makes the `CS_PbfParticles` vs `CS_FluidClear` layout failure reappear. Found a real latent bug on the way (`pbf_particles` 20-DWORD cbuffer vs 16 declared, which `ComputePipeline::SetValidatedLayout` rejects); fixed by Claude in `bbe1249` + `295dc6c`. |
| WO-3 docs | qwen-5090 | **draft committed**, checked by the orchestrator; final pass after WO-4 | `7d104ba` | Three files: new `backyard8-ssfr-whitewater.md` (Status → 1. What changed and why, 16 rows incl. Claude's tuning row → 2. Data flow → 3. Config keys and debug views → 4. Cost → 5. Verification → 6. Remaining work), one `README.md` row directly after the `backyard8-ssfr-realism.md` row (line 107), and `backyard8-screen-space-fluid.md` (Status, pipeline diagram, Tuning, debug views 0..9, paper-gap list re-worded). **20 `TBD`s** — 8 in the cost table, 12 in the verification table — verified by the orchestrator: no invented numbers, and the Windows rows are TBD, not claimed. Spot-checked against source: the `2.5f` caustic cap (`CS_ParticleShade.hlsl:728`), the 6/20 neighbourhood thresholds (`CS_PbfDiffuse.hlsl:6-8`) and the anisotropy spray flag (`CS_FluidSplat.hlsl:22`, binding t2) all check out. The orchestrator corrected one factual error before the final pass: the data-flow diagram had the dispatch order wrong (`CS_PbfWhitewater` before the drying pass) and described `CS_PbfDiffuse` as *reading* `BackyardWaterDiffuse` — in `ComputeWhitewater()` the order is `whitewater-dry` (step 1) → `whitewater` (2) → `whitewater-diffuse` (3) → `whitewater-caustics` (4), and steps 3/4 bind both buffers as a single read-write each. Diagram fixed and step numbers added. |
| WO-4 validation + backends | qwen-local | **done for Linux/GL; Windows = environment block**, checked by the orchestrator | `2105406` | 8 files: 7 GL twins (`CS_FluidClear`, `CS_FluidSplat`, `CS_ParticleShade` modified; `CS_FluidWhitewater`, `CS_PbfWhitewater`, `CS_PbfDiffuse`, `CS_PbfFields` new) + `backyard8_references.json`. **Twin reproducibility re-checked by me**: regenerating all seven with `$HOME/src/SPIRV-Cross/build/spirv-cross` leaves `git diff` empty for the shaders, so the twins are generated, not hand-edited. `check_gl_twins.py` → **PASS**: 58 kernels, 53 twins, exit 0 (was 10 problems). `check_fluid_constants.py` → PASS, exit 0. `--game-selftest` → **78 PASS, 0 FAIL, exit 0** (my run). Full `backyard8_regression.py` re-run by me → **RESULT: PASS**, exit 0: screen-space 0.002 / 0.792 / 0.530 % (f24/f90/f300), splat 0.003 / 0.039 / 0.047 %, spheres 0.001 / 0.151 / 0.129 %, dbox ≤ 4; lake −0.0433 m, water −0.1697 m. Re-freeze was **scoped correctly**: the JSON diff touches only `references["screen-space"]` f24/f90/f300 (`tolerances`, `splat`, `spheres`, `water_level` untouched), and `splat`/`spheres` pass **without** re-freezing. Cost (Vulkan 960×540, `--profile --profileFrames 400`, default camera, `GPU_N=397`) reproduced by me on 3 consecutive water runs and 1 lake run: water Fluid Particles 0.013 / Fluid Clear 0.006 / **Fluid Splat 0.159-0.162** / Resolve 0.004 / Smooth 0.004-0.012 each / **Whitewater Splat 0.084-0.088** / **shade 0.074** ms; lake 0.008 / 0.006 / **0.119** / 0.004 / 0.004-0.006 / **0.005** / **0.044** ms. A cold first run reads ~1.5× higher (0.25 ms splat) — note for future runs. **`CS_Pbf*` solver times: not in the capture** — verified there is no `ProfileScopeGuard`/`RegisterScope` in `PbfFluid.cpp`, so `--profile` cannot see them; reported as absent, not 0. **Windows (d3d12/d3d11/gl): environment block — not run**, no Windows host from this machine; commands + risks recorded in the doc §5.3. |
| WO-3 docs (final pass) | qwen-5090 + orchestrator | **done** | `7d104ba` (+ this doc commit) | Draft had 20 `TBD`s; the final pass replaces them with WO-4's numbers as re-verified by me, rewrites the Status line, splits verification into 5.1 Linux / 5.2 the re-freeze rationale / 5.3 Windows (**not run**), adds the per-API frame-300 back-buffer requirement, and rewrites §6 (Windows unrun, solver GPU time unmeasurable without instrumentation, screen-space references Vulkan-only). Corrected in review: data-flow dispatch order (`whitewater-dry` is step 1; `diffuse`/`caustics` bind their buffers read-write). `TBD` count now 0; the README row and the `backyard8-screen-space-fluid.md` status line no longer say "draft/TBD". |

## Notes from Claude

- Debug views 7-9 only work after WO-1 raises the `debugView` clamp in `BuildParticleDesc`
  (it is still 0..6, so 7-9 silently show view 6). Check all three in WO-1's acceptance run.
- WO-3: add the tuning row above to the design summary; WO-4 replaces the cost numbers with its
  own measurements.

- **Review of WO-2 (`968f450`): not accepted yet.** `check_fluid_constants.py` fails on the
  branch, so the acceptance check ("passes on the branch") is not met. Of the two failures:
  1. `CS_PbfParticles.hlsl` 20 DWORDs vs manifest 16 was a REAL bug (the kernel reads
     `TileCountY`, which was never uploaded). Claude fixed it in `bbe1249` (manifest and C++
     struct now 20). Nice catch.
  2. "ParticleConstants: CS_PbfParticles.hlsl has a different layout than CS_FluidClear.hlsl" is
     a false positive. Kernels may reuse a cbuffer NAME with different layouts when they use
     different `constants_builder`s (`fluid_particles` vs `particle_splat`). Fix the checker so it
     compares layouts among kernels that share a `constants_builder` (fall back to the name
     only when a kernel has no builder). Re-run the three mutation tests, then commit as
     `WO-2b: ...` and fill in the WO-2 row.

- **Review of WO-1 (`b060602`): accepted. WO-4 may start.** Evidence:
  - Scope: only the four WO-1 files.
  - Defaults: all 39 new defaults equal `PbfWhitewaterSettings` / `ParticleScreenSpaceDesc`
    and the JSON values.
  - Clamps mirror the solver's and ParticleSystem.cpp's.
  - GUI resize path (`SetWhitewater` + `RegisterParticleSystem`) is GPU-safe on every
    backend: Vulkan/WebGPU retire old buffers, D3D12 keeps them alive per frame, GL/D3D11
    refcount them.
  - Vulkan runs at f70/f120/f240: no `[ERROR]`.
  - Debug views 7/8/9 render their channels.
  - `backyard8_regression.py --skip-captures`: PASS (lake -0.0440, water -0.1705).

- **Review of WO-2b (`68e979b`): accepted; WO-2 is done.** The checker passes on the branch
  (exit 0). Claude ran three independent mutations on a `/tmp` copy (`--shaders`/`--framework-src`),
  all caught with exit 1 and clear messages:
  - manifest `particle_shade` 100 -> 96;
  - two adjacent members of the C++ `ParticleShadeConstants` swapped;
  - `float` -> `float3` in `WhitewaterSplatConstants` (HLSL packing: 28 -> 32 DWORDs, shifted
    offsets reported).
  The `CS_PbfDiffuse.hlsl` mutation was reverted byte-identical.

- **Orchestrator finding (WO-1 acceptance, 2026-11-10): the `screen-space` capture references
  are stale, independently of WO-1.** The full gate at HEAD fails `screen-space` f24 (0.84 %),
  f90 (52.4 %) and f300 (112.1 %) pixels while `splat` and `spheres` pass at 0.003-0.31 %; the
  references were last written at `ec0ee11`, before `3ad6d1a` rewrote the shade and `9f83cd1`
  retuned it. Proven not to be WO-1: running the current binary with a JSON that omits all 16
  new `screenSpace` keys and the `whitewater` block gives 295,441 px / mean (45.8, 91.7, 109.6)
  at f90 vs 295,115 px / (45.9, 91.9, 109.9) with the shipped JSON — i.e. the config values are
  not the cause. WO-4 §2 therefore has to re-freeze `screen-space` (only) after sign-off, and
  must show `splat`/`spheres` still pass unchanged.

## Requests to Claude

- **`compute_kernels.json` `pbf_particles` is still 16, not 20.** `bbe1249` only changed
  `FluidParticleComputeConstants` (now 80 bytes, uploads `TileCountY`); the manifest entry for
  `pbf_particles` still declares `constant_count: 16`, so the declared count and the 20-DWORD
  cbuffer still disagree and `ComputePipeline::SetValidatedLayout` would reject the pipeline
  (`BaseDriver.cpp:95` compares declared vs reflected). Measured 2026-11-10:
  `git log -1 -- T850/Assets/Shaders/compute_kernels.json` is still `3ad6d1a`, and
  `check_fluid_constants.py` reports `CS_PbfParticles.hlsl ... is 20 DWORDs but
  compute_kernels.json (pbf_particles) declares constant_count 16`. Please bump it to 20 in
  the same commit as any shader edit (WO-4 must not touch this file).
  - **Claude: done in `295dc6c`** (manifest 20). You were right; `bbe1249` had missed it. The
    checker now reports only the `ParticleConstants` name collision that WO-2b fixes.
- **`CS_PbfParticles.hlsl` reuses the cbuffer name `ParticleConstants`** with a layout that has
  nothing to do with `CS_FluidClear`/`CS_FluidSplat`/`CS_ParticleSplat`. Per your WO-2b note
  the checker will group by `constants_builder`; renaming this one to
  `PbfParticlesConstants` would additionally make the name unambiguous for readers and for the
  GL-twin generator. Your call — the checker no longer needs it.
  - **Claude: declined.** Renaming would change a shader and its GL twin for a kernel with no
    dispatch site. Grouping by `constants_builder` in WO-2b is the real fix; keep the name.
