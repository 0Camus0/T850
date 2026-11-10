# Diagnosis 3 — Implementation Status

> **Superseded in part (2026-11-09):** the Gaussian smoothing, thickness-based alpha, offset
> reflection/refraction and the averaged splat look described here were replaced on Vulkan. See
> [backyard8-fluid-render-d3d-port.md](backyard8-fluid-render-d3d-port.md) for what changed and
> the D3D11/D3D12 port steps.

Status: implemented 2026-11-08 against the [Diagnosis 3](diagnosis-3.md) plan, on top of
commit `90f0550`. Vulkan, D3D12 (Proton) and OpenGL (Windows build under Wine) now produce the
same particle layer at frame 24. Native D3D11 was not run; the Proton D3D11 result is
geometry-correct but colour-wrong because Proton's D3D compiler is Wine's builtin, as
diagnosis-3 F2 explains.

This file records what was built, the measured results, and the deliberate deviations. It is
the current companion to [backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md).

## 1. Implemented

### Phase 0 — blockers and correctness

| ID | Change | State |
|---|---|---|
| F1 | `CS_ParticleShade` uses `SampleLevel` for the scene colour (native D3D11 `cs_5_0` cannot use implicit derivatives) | done |
| F3 | Normals are reconstructed from view-space positions and a cross product; the mirrored x and the `-dl/-du` sign flip are gone | done, verified 100% quadrant signs |
| F9 | Refraction/reflection use `float2(n.x, -n.y)` because UV y grows down and view y grows up | done |
| F10 | Scene depth is linearised with `Projection.m[2][2]` / `[3][2]`, not the view-projection rows | done in `CS_ParticleSplat` and `CS_FluidSplat` |
| F5 | Surface depth keeps the minimum sphere front, as the paper and the occlusion test require | done |
| F7 | Thickness sums unweighted chords and divides by the lattice packing factor (4.19); absorption uses physical per-metre coefficients times an artistic multiplier | done |
| F8 | Shaded colour is composited with alpha 1 inside the fluid; the splat resolve is disabled in screen-space mode; a configurable fallback remains for debugging | done |
| F12 | Depth-radius scale is exposed in config; the shipped value is 1.0 (see deviations) | done |
| F13 | Documentation updated (this file, the screen-space doc, the README and diagnosis links) | done |

### Phase 1 — F32 depth and scatter

- New `CS_FluidClear`, `CS_FluidSplat`, `CS_FluidResolve` kernels.
- Per-frame compute buffers `__fluid_depth_bits_BackyardWater` and
  `__fluid_thickness_bits_BackyardWater`, sized by `$window`.
- `CS_FluidSplat` is one thread per particle and writes only the pixels inside its disc:
  `InterlockedMin` on the float bits for the front depth, `InterlockedAdd` on a 1/1024 m fixed
  point for the thickness. This replaces the per-pixel loop over every particle.
- `CS_FluidResolve` writes `FluidDepthA` as F32 (R32F) and `FluidThickness` as RGBA16F
  (`r` metres, `a` coverage).
- The engine now accepts `"F32"` as the colour format for an `r32float` storage binding;
  before this, no graph could declare an R32F storage target.
- Buffer extents may be **read** (the scatter dispatches over the projected particle table),
  not only written. When a buffer-extent pass names a target, the target's dimensions are used
  for the constants' `OutputWidth/OutputHeight` so projection and pixel math see the screen
  size, not the particle count.

### Phase 2 — smoothing baseline

- `CS_ParticleSmooth` is a separable Gaussian (horizontal + vertical), radius 24 px, F32 source
  and destination, two active passes by default. Empty pixels are skipped and normalised out.
- The graph contains 8 ping-pong passes. Only the configured number is enabled by
  `Backyard8Scene::ApplyRenderModeToGraph`; an odd count enables one extra copy pass so the
  shade always reads `FluidDepthA`.
- Depth is stored and smoothed in F32, so quantisation no longer terraces the normals.

### Phase 3 — shading and composite (partial)

- The shade reads F32 depth and separate thickness/coverage, reconstructs normals by cross
  product, uses the corrected refraction direction, physical absorption and the configurable
  fluid colour.
- Alpha is 1 inside the fluid and fades only at the silhouette via coverage, so the composite
  no longer blends the background in a second time.
- The full-screen graphics shade with environment-cubemap reflection (diagnosis-3 §6.7) is
  **not** done; reflection remains screen-space + sky colour.

### Solver — analytic solid boundary (the "water glued to the walls" fix)

The reported artefact: water formed a standing sheet on the pool walls, visible identically in
the splat and screen-space renderers, 0.5–0.7 m above the real surface. It was a solver
defect, not a renderer one.

**Measured before.** At frames 600 and 1200 (fixed 1/60 s):

- every particle above y = −0.60 was at a distance of **exactly 0.065 m from a wall** — the
  `0.5 × spacing` clamp plane — and none of them was falling (`|v| ≈ 0.08 m/s`, `vy ≈ 0`);
- the wall shell (`d < 0.13 m`) reached y = −0.145 while the interior surface sat at −0.85, so
  a one-particle-thick film stood 0.7 m above the pool and never drained;
- **411 particles in 192 groups shared an identical position**, up to 4 per point, at the wall
  corners.

**Cause.** The wall was only a hard position clamp. The fluid next to a wall is packed about
25 % denser than the rest lattice, but the density sum only sees fluid and still reads ≈ 1.0.
The constraint therefore believed the wall layer was at rest (`λ ≈ 0`) and the interior's
pressure was free to squeeze it along the wall; the clamp cancelled the horizontal part of
every correction, so the squeeze came out **vertically** — a ratchet. Once particles were
pinned to the clamp plane and to each other, the Spiky gradient at r = 0 returned zero, so
coincident particles could never separate again, which locked the film in place.

**Fix — Akinci et al. 2012 boundary handling in continuous (analytic) form.** The container is
a set of analytic surfaces, so the missing half-space of the Poly6 kernel is integrated in
closed form instead of sampled with boundary particles:

```
D(d)  = 0.5 * P(d/h) / P(0)          P(u) = (1-u) - 4/3(1-u³) + 6/5(1-u⁵) - 4/7(1-u⁷) + 1/9(1-u⁹)
D'(d) = -(315 / (256 h)) (1-(d/h)²)⁴
```

`D(0) = 0.5` (a particle on the face misses half its neighbourhood) and `D(d ≥ h) = 0`. The
boundary now contributes to **all three** places a fluid neighbour does:

| Term | Contribution |
|---|---|
| density (eq 2) | `+ D(d)` |
| gradient sum / λ (eqs 8, 11) | `+ D'(d) n`, and `+ D'(d)²` to the CFM denominator |
| position correction (eq 14) | `+ (λ_i + λ_b) D'(d) n` with `λ_b = λ_i` (Akinci) |

`n` points from the solid into the fluid, so `D'(d) n` points toward the solid — the same
convention as `SpikyGrad` (i → neighbour). Applied to the pool floor, the four walls (up to the
rim) and the three step AABBs (nearest point, so edges/corners get the right normal). Corner
faces are summed without the pairwise overlap correction; that slightly over-counts the
corner quarter-space, which is the safe direction (it prevents corner jamming).

Two related defects fixed with it:

1. **The wall stopped at the water line, not the rim.** `Collide` gated the wall clamp on
   `p.y < WaterPlaneY` (−0.04) while the pool wall is solid to `waterPlaneY + freeboard`
   (0.38). That left a 0.42 m hole: a splash crossed the wall and the deck branch snapped it to
   y = 0.185. There is now a `WallTopY` constant from the layout's `freeboard`; the top stays
   open above it.
2. **Stranded particles were never retired.** Anything that crossed the wall and was below the
   rim sat at the deck clamp height inside the coping stone forever (242 particles), because
   the cull only retired particles 0.6 m outside the footprint. `CS_PbfCull` now retires
   "outside the footprint and below the rim".

Coincident particles also get a deterministic direction (`PairOffset`): when `r² < 1e-8` the
gradient is evaluated at r = 1e-4 along a hash of the particle ids, so a coincident pair always
has a finite repulsion instead of exactly zero.

**Measured after** (same runs, same frames):

| Metric | Before | After |
|---|---|---|
| Particles inside the pool above y = −0.60, frame 600 | 243 (all at d = 0.065) | **0** |
| Highest particle inside the footprint, frame 1200 | −0.145 (wall film) | **−0.907** (free surface) |
| Wall shell (`d < 0.13`) highest particle, frame 600 | −0.145 | −0.930 |
| Settled surface flatness across x (frame 1200) | −0.15 at the walls, −1.06 interior | **−0.91 … −0.93 everywhere** |
| Coincident position groups | 192 (411 particles, up to 4 each) | **0** |
| Particles stranded below the rim outside the footprint | 242 | **0** (retired) |
| Density at the wall shell (fluid + boundary) | 0.999 fluid-only | 1.010 (fluid-only neighbours 21 vs 31 interior) |
| Vulkan frame 24 particle target | 122,702 px, bbox `x[264,695] y[0,342]` | unchanged |

The free-fall frame 24 is bit-identical in geometry because the boundary term only acts within
one smoothing radius of a face, and the seed sphere is 3 m above the rim.

## 2. Verification

| Check | Result |
|---|---|
| Linux/Vulkan `--game-selftest` | 78 PASS / 0 FAIL |
| Windows MSBuild (Release x64) | BUILD SUCCEEDED |
| `check_gl_twins.py` | PASS, 52 kernels / 47 generated twins |
| Particle GLSL twins with `glslangValidator` | 8/8 clean |
| Vulkan frame 24 particle target | 122,702 px, bbox `x[264,695] y[0,342]`, mean (11.1, 29.7, 32.8) |
| D3D12 (Proton) frame 24 | 122,702 px, same bbox, mean (11.1, 29.7, 32.8) |
| OpenGL (Wine) frame 24 | 122,702 px, same bbox, mean (11.1, 29.6, 32.6) |
| OpenGL frame 300 | 85,115 px, bbox `x[0,959] y[269,479]`, mean (12.1, 22.8, 25.5); visually matches the Vulkan settled pool |
| Normal quadrant test (frame 24, debug view 1) | left n.x −0.54 / 100% correct, right +0.53 / 100%, bottom n.y −0.70 / 100% |
| D3D11 (Proton) frame 24 | 122,702 px, same bbox, but mean (59.6, 60.4, 60.4): geometry correct, shading wrong. Debug views confirm depth is present but thickness resolves to ~54.8/255 (about 0.2 m instead of several metres) and normals to ~60.4/255, so Wine's compiler is miscompiling the thickness/normal math (F2). Native D3D11 is still the missing test. |
| Solver wall-adhesion (see "analytic solid boundary") | film gone, surface flat within 2 cm, 0 coincident groups, frame 24 target unchanged |
| Vulkan `--game-selftest` after the boundary change | 78 PASS / 0 FAIL |
| Windows MSBuild after the boundary change | EXIT=0 |
| `check_gl_twins.py` after regenerating 10 PBF twins | PASS |
| 10 regenerated PBF GLSL twins, `glslangValidator -S comp` | 10/10 clean |

The Vulkan frame 300 now shows a continuous blue pool with a specular waterline and the pool
walls refracted through the water; the old dark band and the octagonal normal terraces are
gone.

## 3. Deliberate deviations from diagnosis-3

1. **Smoothing is a Gaussian, not the exact curvature flow (F6).** The exact mean-curvature
   update was implemented and tested at 8 and 40 full-resolution iterations. It reduced but did
   not remove the cubic-lattice facets; a 2-pass separable Gaussian with a 24 px radius did.
   The current shader is the Gaussian baseline. `depthTolerance` is retained in the config for
   a future curvature-flow variant but is unused by the shipped kernel. Exact curvature flow
   needs the paper's 40–100 iterations (or half resolution), which is the next quality step.
2. **Depth-radius scale is 1.0, not 0.6–0.75 (F12).** With 0.7 the particle depth discs leave
   holes at grazing angles because the scatter does not fill empty pixels and the smoothing
   treats zero depth as a boundary. 1.0 covers the surface; the trade-off is that the geometric
   surface sits about half a spacing outside the particle centres. The scale is configurable
   for when a hole-fill or half-resolution path lands.
3. **Normal stencil is 2 px by default**, not 1. The Gaussian removes most of the lattice; the
   2 px difference cleans the remainder. `normalStep` remains a config/UI control.
4. **The splat fallback defaults off.** The correct Vulkan/D3D12/GL path has no splat
   underneath. The `splatFallback` checkbox restores the old fallback for debugging or for a
   backend whose screen-space path is known broken (Proton D3D11 today).
5. **Thickness uses the spacing-sized sphere and the 4.19 packing factor**, while the depth
   sphere uses the configured depth radius. This matches diagnosis-3 F7: the depth wants a
   smaller surface sphere, the thickness wants the physical chord sum.
6. **Boundary faces are summed independently in corners.** The analytic half-space integral
   is exact for one infinite plane; at an inside corner the two face integrals both count the
   shared quadrant, so the corner is over-dense by up to 25 % of one face's deficit. That is
   the safe direction (it keeps particles out of corners) and the affected volume is small.
   Exact inclusion–exclusion needs the 2-D half-space integral per corner.
7. **The graph has 8 smoothing passes but ships with 2 enabled.** This keeps the option to
   raise the count without a graph edit while paying only for the enabled passes. Pipeline
   creation still happens for the disabled passes at load; skipping that is a small follow-up.

## 4. Still difficult / not done

1. **Exact curvature flow at the paper's quality.** The Gaussian works but is not the paper's
   surface-tension-like flow (8 passes are available; the paper uses 40–100 at half
   resolution). It also blurs the depth across the silhouette by roughly the
   kernel radius; the thickness coverage masks the result, but a bilateral/curvature variant
   would preserve a sharper profile.
2. **Half-resolution processing.** The doc's 6.9: `$window_half` sizing, bilateral upsample,
   scaled focal lengths and tolerance. This is the main performance lever and the prerequisite
   for 40–100 curvature-flow iterations.
3. **Full-screen graphics shade with IBL.** The compute shade has no cubemap binding, so
   reflection is a screen-space lookup + sky blend. Moving it to `FS_Quad` with
   `bind_environment_map` is the next visual step.
4. **Perlin noise/foam and spray classification.** Not implemented. The solver density buffer
   is still not published to the renderer; spray particles render in the same surface.
5. **Native D3D11 verification.** The code now uses `SampleLevel`; Proton D3D11 still uses
   Wine's compiler and produces wrong colour (F2). Native Windows needs to run the same frame
   tests.
6. **Performance measurement.** The scatter removes the screen-coverage × particle-count
   loop. Frame-time capture with GPU timestamps is still needed to confirm the Phase 1 target
   (screen-space minus splat-only ≤ 1.5 ms at 1080p).
7. **R5 durable scatter and the Phase 4 lifecycle items** remain from the earlier plans.

## 5. Repro

```bash
export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu
# Vulkan normal test
./DayScene --unattended --api vulkan --scene 8 --backyard-no-hud \
  --width 960 --height 540 --regressionFixedDt 0.0166667 \
  --postProcessMode compute --dump-frame 24 --logLevel info

# Debug views: 1 normals, 2 depth, 3 thickness, 4 coverage
# (copy Backyard8Fluid.json, set screenSpace.debugView, pass --fluid-config <abs path>)

# Runtime switch
... --fluid-render splat
... --fluid-render screen-space
```

# Solver boundary probe: dump the particle fields and a Y histogram at chosen frames
T850_PBF_DUMP=120,300,600,1200 T850_PBF_DUMP_DIR=/tmp/opencode \
  ./DayScene --unattended --api vulkan --scene 8 --backyard-no-hud \
  --width 640 --height 360 --regressionFixedDt 0.0166667 --dump-frame 1200 \
  --fluid-config /tmp/opencode/fluid_splat.json --logLevel info
# writes pbf_f<frame>.csv (x,y,z,alive,vx,vy,vz,density,lambda,neighbours)

GL twins: `scripts/shaders/generate_gl_compute_twins.ps1 -RootDir <repo> -SdkRoot <VulkanSDK> \
  -Kernel CS_PbfDensity,CS_PbfSolve,CS_PbfIntegrate,CS_PbfUpdateVelocity,CS_PbfCull \
  -Force` on a host with the Vulkan SDK, then
`python3 scripts/validation/check_gl_twins.py --shaders Assets/Shaders`.

## 6. File map (changed by this implementation)

| Concern | File |
|---|---|
| Clear / scatter / resolve | `T850/Assets/Shaders/CS_FluidClear.hlsl`, `CS_FluidSplat.hlsl`, `CS_FluidResolve.hlsl` |
| Smoothing | `T850/Assets/Shaders/CS_ParticleSmooth.hlsl` |
| Shading | `T850/Assets/Shaders/CS_ParticleShade.hlsl` |
| Splat (F10 + style) | `T850/Assets/Shaders/CS_ParticleSplat.hlsl` |
| Manifest | `T850/Assets/Shaders/compute_kernels.json` |
| Graph (F32 RTs, buffers, 8 ping-pong passes) | `T850/Assets/Scenes/Backyard8Scene_RenderGraph.json` |
| Constants and builders | `T850/Framework/src/utils/ComputeKernelRegistry.cpp` |
| Extent/size validation, R32F format, target size | `T850/Framework/src/scene/RenderGraph.cpp` |
| Descriptor fields | `T850/Framework/include/scene/ParticleSystem.h` |
| Scene pass enable/disable, config mapping | `T850/DayScene/Backyard8Scene.{h,cpp}`, `Backyard8FluidJson.h` |
| Scene config | `T850/Assets/Scenes/Backyard8Fluid.json` |
| Solver boundary (density/gradient/pressure) | `T850/Assets/Shaders/CS_PbfDensity.hlsl`, `CS_PbfSolve.hlsl` |
| Wall extent (`WallTopY`), wall clamp, contact velocity | `CS_PbfIntegrate.hlsl`, `CS_PbfSolve.hlsl`, `CS_PbfUpdateVelocity.hlsl` |
| Stranded-particle retirement | `CS_PbfCull.hlsl` |
| Boundary constants (`wallTopY`, solid-box binding on the density pass) | `T850/Framework/src/simulation/PbfFluid.cpp`, `PbfFluid.h`, `compute_kernels.json` |
| Particle-field dump (`T850_PBF_DUMP`) | `T850/DayScene/Backyard8Scene.{h,cpp}`, `PbfFluid::ReadParticleFields` |
| GL twins | 47 generated files under `T850/Assets/Shaders/` |
