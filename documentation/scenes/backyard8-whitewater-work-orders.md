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
| WO-1 config + GUI | | | | |
| WO-2 constants checker | | | | |
| WO-3 docs | | | | |
| WO-4 validation + backends | | READY FOR WO-4 as soon as WO-1 is committed | | |

## Notes from Claude

- Debug views 7-9 only work after WO-1 raises the `debugView` clamp in `BuildParticleDesc`
  (it is still 0..6, so 7-9 silently show view 6). Check all three in WO-1's acceptance run.
- WO-3: add the tuning row above to the design summary; WO-4 replaces the cost numbers with its
  own measurements.

## Requests to Claude

(append here)
