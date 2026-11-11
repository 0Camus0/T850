# Backyard 8: whitewater, environment reflections and G-buffer water lighting (2026-11-10)

Status: **implemented and verified on Vulkan (native Linux)**. D3D12, D3D11 and OpenGL were
**not run** on this tree (no Windows host); their gate is §5.3 and is an open item, not a pass.

The implementation is checkpointed in `3ad6d1a` ("WIP Backyard 8: whitewater, env reflections,
G-buffer lighting, half-res SSFR") and tuned in `9f83cd1` ("Backyard 8: tune whitewater and
half-resolution splat"); the config keys and Dev GUI landed in WO-1 (`b060602`); the GL twins and
the screen-space references landed in WO-4 (`2105406`). Cost and verification numbers below are
WO-4's measurements, re-run independently by the orchestrator (§4, §5). Preceding record:
[backyard8-ssfr-realism.md](backyard8-ssfr-realism.md). Pipeline reference:
[backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md).

## 1. What changed and why

One row per artifact/improvement, from the design summary in
[backyard8-whitewater-work-orders.md](backyard8-whitewater-work-orders.md) and the checkpoint
commit. All files are relative to `T850/`.

| # | Item | What | Why it was needed | Files |
|---|---|---|---|---|
| 1 | Off-screen reflections | Reflected rays the screen trace cannot resolve sample the scene's sky cube (`@environment_map`, a new cube binding for compute passes); on-screen hits keep SSR, and rays that exit the frame keep the exit-pixel continuation. `EnvRadiance` in the shade. | The old fallback was a flat sky gradient plus exit-pixel continuation, so the house above the frame never appeared in reflections (R7 in the realism record). | `Framework/src/scene/RenderGraph.cpp`, `Framework/src/utils/ComputeKernelRegistry.cpp`, `Framework/src/utils/ComputeReflection.cpp`, `Framework/src/video/{d3d11,d3d12,vulkan,webgpu}/*`, `Assets/Shaders/CS_ParticleShade.hlsl` |
| 2 | SSR false hits | A screen-space crossing is accepted only if the G-buffer normal at the hit faces the ray (`TraceReflection`). | Rays tilted back toward the camera "hit" the top of the near coping (R7). | `Assets/Shaders/CS_ParticleShade.hlsl` |
| 3 | Refraction under the coping | The refracted ray is marched through the depth buffer; a hidden hit takes the analytic pool-box distance (`PoolBoxExit`) and the last visible sample (`MarchRefraction`). | Refraction lookups that left the pool fell back to the unrefracted pixel, producing jagged islands (R6). | `Assets/Shaders/CS_ParticleShade.hlsl` |
| 4 | Pool behind a splash sheet | Water in the air shades the resting pool surface behind it as a second interface (`PoolSurfaceBehind`). | A breaking sheet of water should still show the pool through it, not the sky. | `Assets/Shaders/CS_ParticleShade.hlsl` |
| 5 | Spray classification | Per-particle spray flag from the anisotropy pass (`CS_PbfAnisotropy` w-channel) instead of the screen-ring heuristic; splatted into `CS_FluidSplat` aux channel 3. | The screen-space "isolated droplet" test misclassified water in the air; the solver knows which particles are actually airborne. | `Assets/Shaders/CS_FluidSplat.hlsl`, `Assets/Shaders/CS_PbfAnisotropy.hlsl` |
| 6 | Sunlight under water | Direct sun rebuilt from G-buffer albedo and normal and `ShadowAccum`, absorbed along the refracted sun path (`SubmergedRadiance`); caustics only where the sun reaches (`SurfaceSunVisibility`). | The shade had no water column: the pool read as a milky volume with a flat sky colour (R5). | `Assets/Shaders/CS_ParticleShade.hlsl` |
| 7 | Simulated caustics | Focusing 1/(1 + c·d·∇²h) of the smoothed PBF column heights modulates the procedural web; `CS_PbfFields` mode 1 writes the caustic focus field, `causticFocus` scales it. | Procedural caustics alone do not move with the simulated surface. | `Assets/Shaders/CS_PbfFields.hlsl`, `Assets/Shaders/CS_ParticleShade.hlsl` |
| 8 | In-scatter | Sun (Henyey–Greenstein, surface shadow) and sky light scattered toward the eye replace the constant fluid colour (`InScatter`). | Clear pool water is not a constant colour: light is scattered by the water itself. | `Assets/Shaders/CS_ParticleShade.hlsl` |
| 9 | Sun glitter | Slope variance of ripples too fine for the pixel widens the glint and reflection cone (Toksvig, `RippleGradient` + main kernel). | Sub-particle ripples were invisible, so the surface read as a perfect mirror (R5). | `Assets/Shaders/CS_ParticleShade.hlsl` |
| 10 | Flow ripples | Ripple slope follows per-particle agitation (smoothed speed); hashed, irregular wave set (`CS_PbfWhitewater` z-channel, `RippleGradient`). | Wind ripples were uniform; real pool water ripples where the flow is fast. | `Assets/Shaders/CS_PbfWhitewater.hlsl`, `Assets/Shaders/CS_ParticleShade.hlsl` |
| 11 | Backlit crests, meniscus | Thin water toward the sun glows (`backlight`); brighter contact line at walls (`contactLine`). | Free surfaces and wall contact are the most characteristic features of pool water. | `Assets/Shaders/CS_ParticleShade.hlsl` |
| 12 | Half resolution | Depth/thickness splat and smoothing at half resolution (`$window_half` targets `FluidDepthA/B`, `FluidThickness`), depth-aware upsample in the shade; the splat tests the farthest full-resolution depth of each half pixel. | The close-camera splat cost was the one-thread-per-particle scatter at full resolution (realism record §4); halving the target is the paper's main performance lever. | `Assets/Scenes/Backyard8Scene_RenderGraph.json`, `Assets/Shaders/CS_FluidSplat.hlsl`, `Assets/Shaders/CS_ParticleShade.hlsl`, `Framework/src/scene/RenderGraph.cpp` (`$window_half`) |
| 13 | Foam on the water | Ihmsen 2012 potentials (trapped air, crest, energy) in `CS_PbfWhitewater`; per-particle foam and entrained air that decay; foam lace texture; milky aeration. | The water start's breaking crests had no foam. | `Assets/Shaders/CS_PbfWhitewater.hlsl`, `Assets/Shaders/CS_FluidSplat.hlsl` (aux 0..2), `Assets/Shaders/CS_ParticleShade.hlsl` |
| 14 | Spray, foam, bubbles | Secondary particles in a ring buffer, classified by fluid neighbours (spray < 6, foam 6..20, bubble > 20), splatted into per-pixel channels by `CS_FluidWhitewater`; bubbles dimmed by the water above. | Isolated droplets drawn as chrome balls read as speckle; a density field of soft blobs reads as mist, lace and milky water. | `Assets/Shaders/CS_PbfWhitewater.hlsl`, `Assets/Shaders/CS_PbfDiffuse.hlsl`, `Assets/Shaders/CS_FluidWhitewater.hlsl` |
| 15 | Wet deck | Water landing on the deck/coping stamps a world-space wetness map that dries (45 s e-folding); wet surfaces are darker with a sky-reflecting film. | A splash that leaves no trace on the deck breaks the illusion. | `Assets/Shaders/CS_PbfWhitewater.hlsl`, `Assets/Shaders/CS_PbfDiffuse.hlsl`, `Assets/Shaders/CS_PbfFields.hlsl` (mode 0), `Assets/Shaders/CS_ParticleShade.hlsl` |
| 16 | Claude's tuning pass (`9f83cd1`) | Potentials calibrated on particle dumps of the water start (trapped air 3..15, energy 0.5..10, crest 0.3..2; a calm pool never emits); shorter freshwater lifetimes (foam 1.5 s, aeration 1.2 s, secondary particles 2 s at full decay); secondary particles drawn as soft density blobs instead of pixel dots; foam lace modulates opacity; isolated splash particles shaded as whitewater; caustics capped at 2.5×; the half-res splat no longer tests opaque depth (the shade re-tests at full resolution). | The checkpoint's look was untuned: "foam covers too much of the surface after the drop". | `Assets/Shaders/CS_PbfWhitewater.hlsl`, `CS_PbfDiffuse.hlsl`, `CS_FluidWhitewater.hlsl`, `CS_FluidSplat.hlsl`, `CS_ParticleShade.hlsl` (per the commit message) |

## 2. Data flow

The solver's whitewater kernels run after the PBF step and publish four buffers through the
particle buffer registry (`PbfFluid::PublishWhitewaterBuffers`, `T850/Framework/src/simulation/PbfFluid.cpp`);
the render graph binds them by the names in `T850/DayScene/Backyard8FluidJson.h`
(`BackyardWaterWhitewater`, `BackyardWaterDiffuse`, `BackyardWaterCaustics`, `BackyardWaterWetness`).

```
 PBF solver step (CS_PbfIntegrate / CS_PbfSolve / ... / CS_PbfCull, CS_PbfAnisotropy)
   │  positions, velocities, grid, column heights, anisotropy (spray flag)
   ▼
 CS_PbfFields mode 0 (dispatch "whitewater-dry")            [ComputeWhitewater step 1]
   │  reads:  column heights
   │  writes: BackyardWaterWetness (drying, 45 s e-folding), caustic field
   │  runs FIRST, so this frame's water lands on an already-dried deck
   ▼
 CS_PbfWhitewater (dispatch "whitewater")                   [step 2]
   │  reads:  positions, velocities, grid
   │  writes: BackyardWaterWhitewater (per-particle foam, aeration, agitation, trapped air)
   │          BackyardWaterDiffuse (secondary pool: emit spray/foam/bubbles)
   │          BackyardWaterWetness (stamp where water lands on the deck)
   ▼
 CS_PbfDiffuse (dispatch "whitewater-diffuse")              [step 3]
   │  reads:  positions, velocities, grid
   │  reads/writes: BackyardWaterDiffuse (classify by fluid neighbours: spray < 6, foam 6..20,
   │          bubble > 20; move, land, age out — the buffer is a single read-write binding)
   │  reads/writes: BackyardWaterWetness (landing)
   ▼
 CS_PbfFields mode 1 (dispatch "whitewater-caustics")       [step 4]
   │  reads:  column heights
   │  writes: BackyardWaterWetness, caustic field
   │  publishes BackyardWaterCaustics (focusing 1/(1 + c·d·∇²h) of the smoothed surface)
   ▼
 publish: BackyardWaterWhitewater / BackyardWaterDiffuse /
          BackyardWaterCaustics / BackyardWaterWetness
   ▼
 render graph (Assets/Scenes/Backyard8Scene_RenderGraph.json), screen-space mode:
   Fluid Clear        CS_FluidClear      → __fluid_depth_bits / __fluid_thickness_bits /
                                           __fluid_aux_bits (half-res targets, $window_half)
   Fluid Splat        CS_FluidSplat      reads GBuffer:DEPTH, BackyardWaterAnisotropy,
                                           BackyardWaterWhitewater; writes depth/thickness bits
                                           and aux channels (0 foam, 1 aeration, 2 agitation,
                                           3 spray)
   Fluid Resolve      CS_FluidResolve    → FluidDepthA (F32, half res), FluidThickness (RGBA16F)
   Fluid Smooth 1..8  CS_ParticleSmooth  a-trous ping-pong A↔B (half res; 6 enabled by the
                                           shipped JSON, an odd count copies B back to A)
   Whitewater Splat   CS_FluidWhitewater reads BackyardWaterDiffuse, GBuffer:DEPTH,
                                           FluidDepthA; writes __whitewater_bits
                                           (0 spray, 1 foam, 2 bubbles, 3 sparkle)
   Fluid Surface Shade CS_ParticleShade  reads FluidDepthA, FluidThickness, Deferred:COLOR0,
                                           GBuffer:DEPTH, @environment_map (cube),
                                           GBuffer:COLOR0/1, ShadowAccum:COLOR0,
                                           __fluid_aux_bits, BackyardWaterCaustics,
                                           BackyardWaterWetness; writes FluidParticles:COLOR0
                                           and clears __whitewater_bits
   Fluid Particle Composite (PARTICLE_COMPOSITE) → over Deferred → final image
```

The four published buffers and their element counts (`PublishWhitewaterBuffers`):
`BackyardWaterWhitewater` (particle count), `BackyardWaterDiffuse` (diffuse capacity, the
dispatch extent of `Whitewater Splat`), `BackyardWaterCaustics` (surface columns,
`surfaceWidth × surfaceDepth`), `BackyardWaterWetness` (wetness texels, `wetWidth × wetHeight`).

Constants blocks (cross-checked by WO-2's `check_fluid_constants.py`):

| cbuffer | DWORDs | Shared by | C++ mirror |
|---|---|---|---|
| `PbfWhitewaterConstants` | 48 (192 bytes) | `CS_PbfWhitewater`, `CS_PbfDiffuse`, `CS_PbfFields` (one layout, `mode` selects the kernel's role) | `struct PbfWhitewaterConstants` in `Framework/src/simulation/PbfFluid.cpp` (`static_assert(sizeof(...) == 192)`); manifest `constant_count` 48 for `pbf_whitewater`, `pbf_diffuse`, `pbf_fields` |
| `ParticleShadeConstants` | 100 (400 bytes) | `CS_ParticleShade` | `Framework/src/utils/ComputeKernelRegistry.cpp` (`static_assert(sizeof(...) == 400)`); manifest `particle_shade` 100 |
| `WhitewaterSplatConstants` | 28 (112 bytes) | `CS_FluidWhitewater` | `Framework/src/utils/ComputeKernelRegistry.cpp` (`static_assert(sizeof(...) == 112)`); manifest `particle_whitewater` 28 |

## 3. Config keys and debug views

All keys live in `T850/Assets/Scenes/Backyard8Fluid.json` (WO-1, `b060602`); the JSON values
equal the C++ defaults in `T850/DayScene/Backyard8FluidJson.h` exactly (the shipped JSON and the
C++ defaults must agree; the schema is strict and rejects unknown keys).

`screenSpace` keys added by WO-1 (grouped as in the Dev GUI):

| Key | Default | What it does |
|---|---|---|
| `envIntensity` | 1.0 | Multiplier on the environment cube for reflected rays the screen trace cannot resolve (0 = sky gradient fallback) |
| `sunAttenuation` | 1.0 | Sunlight absorbed on its way down to submerged surfaces (0 = lit as if in air) |
| `causticFocus` | 1.0 | Caustic focusing by the simulated surface (0 = procedural pattern only) |
| `contactLine` | 0.5 | Brightening of the meniscus where the water meets a wall |
| `backlight` | 1.0 | Thin crests and sheets glowing when seen toward the sun |
| `scatterSun` | 0.6 | In-scattering: sun light scattered toward the eye inside the water |
| `scatterSky` | 0.8 | In-scattering: sky light scattered toward the eye |
| `scatterAnisotropy` | 0.45 | Henyey–Greenstein g of the in-scatter (0 = isotropic, clamped 0..0.95) |
| `rippleCalm` | 0.6 | Ripple slope multiplier for still water |
| `rippleAgitation` | 1.5 | Extra ripple slope at full local agitation |
| `foamStrength` | 1.0 | Surface foam |
| `foamTextureScale` | 0.35 | Foam lace cell size, metres |
| `aeration` | 1.0 | Milky entrained air under the surface |
| `bubbleStrength` | 1.0 | Bubble sparkles |
| `sprayStrength` | 1.0 | Secondary spray droplets |
| `wetness` | 1.0 | Darkening and sheen of wet deck/coping |

New top-level `whitewater` section (mirrors `t850::PbfWhitewaterSettings` field for field):

| Key | Default | What it does |
|---|---|---|
| `enabled` | true | Run the solver's whitewater kernels |
| `trappedAirMin` / `trappedAirMax` | 3.0 / 15.0 | Potential range mapped to 0..1 (calibrated on particle dumps) |
| `crestMin` / `crestMax` | 0.3 / 2.0 | Crest potential range |
| `energyMin` / `energyMax` | 0.5 / 10.0 | Energy potential range |
| `surfaceNeighbours` | 24.0 | At or above: a submerged particle |
| `foamGain` / `foamLifetime` | 3.0 / 1.5 | Per-second foam production at full potential; e-folding seconds |
| `aerationGain` / `aerationLifetime` | 2.5 / 1.2 | Same for entrained air |
| `maxDiffuse` | 65536 | Secondary particle pool (clamped 64..1<<20; changing it recreates the buffers) |
| `sprayRate` / `crestRate` | 12.0 / 8.0 | Secondary particles per second per fluid particle at full trapped air / crest |
| `diffuseLifetime` | 2.0 | Secondary particle life, seconds (randomised 0.5..1.5×) |
| `bubbleBuoyancy` / `bubbleDrag` | 3.0 / 0.5 | Bubble rise (× gravity) and drag |
| `sprayDrag` | 0.4 | Spray drag, per second |
| `dryingTime` | 45.0 | Wet deck drying, e-folding seconds |
| `wetTexel` / `wetResolution` | 0.06 / 320 | Wetness map texel size (m) and resolution (texels per side, centred on the basin; changing either recreates the buffers) |
| `causticGain` | 0.25 | Caustic focusing gain on the simulated surface curvature |

`debugView` now goes to 9: 0 shaded, 1 normals, 2 surface depth, 3 water path, 4 coverage,
5 reflection, 6 refraction, 7 whitewater (r foam, g aeration, b spray), 8 sun light under
water, 9 wetness. The `debugView` clamp in `BuildParticleDesc` was raised from 0..6 to 0..9 in
WO-1 (before that, views 7-9 silently showed view 6).

Dev GUI (`DrawScreenSpacePanel`, `T850/DayScene/Backyard8Scene.cpp`): a "Scene lighting"
section (env intensity, sun attenuation, caustic focus, contact line, backlight, in-scatter
sun/sky/anisotropy, ripple calm/agitation — all `lookChanged`) and a "Whitewater" section: the
screen-space fields (foam, foam cell size, aeration, bubbles, spray, wetness — `lookChanged`)
and the solver fields (spray rate, crest rate, diffuse lifetime, foam gain/lifetime, aeration
gain/lifetime, drying time, max secondary particles, wetness map resolution, wetness texel),
which call `m_pbf.SetWhitewater(...)` and then `RegisterParticleSystem()` — changing
`maxDiffuse`, `wetResolution` or `wetTexel` recreates the solver buffers, and the descriptor's
geometry (pool capacity, wetness map size) is refreshed from `WhitewaterLayout()`.

## 4. Cost

GPU timestamps from `DayScene --unattended --api vulkan --scene 8 --width 960 --height 540
--profile --profileFrames 400`, screen-space mode, default camera, averaged over 400 frames
(`GPU_N=397`). WO-4 measured these; the orchestrator reproduced them on three consecutive runs
(water: Fluid Splat 0.159-0.162 ms, Whitewater Splat 0.084-0.088 ms, shade 0.074 ms; lake:
0.119 / 0.005 / 0.044 ms). A cold first run reads about 1.5x higher (Fluid Splat 0.25 ms) because
the pool settle is slower; take a warm run.

| Pass | GPU time, water start | GPU time, lake start |
|---|---|---|
| Fluid Particles (stamp) | 0.013 ms | 0.008 ms |
| Fluid Clear | 0.006 ms | 0.006 ms |
| Fluid Splat (half res) | 0.160 ms | 0.119 ms |
| Fluid Resolve | 0.004 ms | 0.004 ms |
| Fluid Smooth 1-6 (each) | 0.004-0.012 ms | 0.004-0.006 ms |
| Whitewater Splat | 0.086 ms | 0.005 ms |
| Fluid Surface Shade | 0.074 ms | 0.044 ms |
| CS_PbfWhitewater (solver) | not in the capture | not in the capture |
| CS_PbfDiffuse (solver) | not in the capture | not in the capture |
| CS_PbfFields mode 0 (whitewater-dry) | not in the capture | not in the capture |
| CS_PbfFields mode 1 (whitewater-caustics) | not in the capture | not in the capture |

The four solver kernels are dispatched from `PbfFluid.cpp`, not from the render graph, and no
`ProfileScopeGuard` exists around them, so `--profile` (render-graph scopes only) cannot see
them. They are reported as absent rather than as 0; measuring them needs instrumentation in
`PbfFluid.cpp` (see §6). The `Whitewater Splat` figure is a 400-frame average, so the splash
frames are a fraction of it; the checkpoint's 1080p figure of 0.17 ms during the splash (§1) is
not comparable to this 960x540 average.

## 5. Verification

### 5.1 Linux (Vulkan, native)

Numbers below were produced by WO-4 (`2105406`) and re-run independently by the orchestrator on
the same tree.

| Check | Result |
|---|---|
| Linux build (`cmake --build build/linux --target DayScene`) | **pass** (exit 0; binary `T850/bin/SteamDeck/Release/DayScene`) |
| `DayScene --unattended --game-selftest` | **pass**: 78 PASS, 0 FAIL, exit 0 |
| `scripts/validation/check_fluid_constants.py` | **pass** (exit 0): 15 cbuffer names, 46 kernels, 51 `constant_count`s, 3 struct/kernel cross-checks |
| `scripts/validation/check_gl_twins.py` | **pass** (exit 0): 58 kernels, 53 twins match their HLSL and the registered bindings |
| GL twin reproducibility | **pass**: regenerating the seven twins with the pinned SPIRV-Cross reproduces them byte-for-byte (`git diff` empty) |
| `backyard8_regression.py` — `splat` captures | **pass**: f24 0.003 %, f90 0.039 %, f300 0.047 % pixels, dbox 0 |
| `backyard8_regression.py` — `spheres` captures | **pass**: f24 0.001 %, f90 0.151 %, f300 0.129 % pixels, dbox <= 1 |
| `backyard8_regression.py` — `screen-space` captures | **pass after re-freeze**: f24 0.002 %, f90 0.792 %, f300 0.530 % pixels, dbox <= 4 |
| Water levels (lake, water start) | **pass**: lake -0.0433 m (target -0.040 +/- 0.015), water -0.1697 m (target -0.170 +/- 0.020) |
| Full gate | `RESULT: PASS`, exit 0 |

### 5.2 The screen-space reference re-freeze

The `screen-space` references in `scripts/validation/backyard8_references.json` were last written
at `ec0ee11`, before `3ad6d1a` rewrote `CS_ParticleShade.hlsl` and `9f83cd1` retuned it, so they
were stale by construction and the gate failed them (f90 50.6 %, f300 113.9 % pixels) while
`splat` and `spheres` passed at <= 0.31 %. The orchestrator showed this is not a config problem:
running with a JSON that omits the new WO-1 keys gives the same f90 statistics. Only the three
`screen-space` entries were re-frozen (WO-4 §2), after Claude's visual sign-off; `splat`,
`spheres`, `tolerances` and `water_level` are untouched, which is why they still pass without
re-freezing.

### 5.3 Windows (native x64 Release, not Proton) — **not run**

D3D12, D3D11 and GL were **not** executed: no Windows host is reachable from the verification
machine. This is an open item, not a pass. Commands (from [backyard8-ssfr-realism.md](backyard8-ssfr-realism.md) §5, run from
`T850/`):

```powershell
python scripts\validation\backyard8_regression.py --api d3d12 --day-scene bin\x64\Release\DayScene.exe
python scripts\validation\backyard8_regression.py --api d3d11 --day-scene bin\x64\Release\DayScene.exe
python scripts\validation\backyard8_regression.py --api gl    --day-scene bin\x64\Release\DayScene.exe
```

Attach one back buffer per API at frame 300 of the water start (`--backyard-start water
--dump-frame 300`) alongside each gate result. A D3D11 run under Proton is not evidence (Wine's
compiler).

Expected risks called out by the work order: the cube map bound to a compute kernel (D3D11/D3D12
SRV of a DDS cube, GL `samplerCube`), half-resolution targets, and the 100-DWORD constant block
(D3D12 root CBV path).

## 6. Remaining work

- **Windows backends unrun**: D3D12, D3D11 and GL have not been verified on this tree; the
  cube-map-in-compute binding and the 100-DWORD constant block are the expected risks (§5.3).
  Run the gate on a native Windows host before calling those backends done.
- **Solver GPU time is unmeasured**: the four `CS_Pbf*` whitewater kernels are dispatched from
  `PbfFluid.cpp` with no `ProfileScopeGuard`, so `--profile` cannot report them (§4). Adding
  scopes around the four dispatches in `ComputeWhitewater()` (plus the main `Step` passes) would
  make the whitewater cost attributable.
- **`screen-space` references are Vulkan-only**: they were re-frozen on Vulkan at
  `2105406` (§5.2). Expect small per-backend differences on Windows; re-freeze per backend only
  once the difference is understood.
- **Exact curvature flow** (the paper's 40-100 iterations) is still not implemented; the shipped
  filter is the edge-aware a-trous chain at half resolution (item 1 of
  [backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md) "What the paper still asks
  for").
- **Exact thickness** (Eq. 9) and optional density-based spray separation remain open (item 5
  of the same list).

## Related documents

- [backyard8-ssfr-realism.md](backyard8-ssfr-realism.md) — the immediately preceding record
  (root causes R1-R9, fixes, cost, Windows commands)
- [backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md) — pipeline reference and
  gap analysis against van der Laan et al. 2009
- [backyard8-particle-fluid-investigation.md](backyard8-particle-fluid-investigation.md) —
  Phase 0/1 and the renderer investigation
- [backyard8-whitewater-work-orders.md](backyard8-whitewater-work-orders.md) — the work orders,
  design summary and status table for this branch
