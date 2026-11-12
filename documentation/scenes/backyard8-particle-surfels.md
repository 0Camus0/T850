# Backyard 8: surfel splatting for the particle renderer (2026-11-11)

Status: **implemented and verified on Vulkan (native Linux)**. D3D11, D3D12 and OpenGL were
**not run** on this tree; the GL twins are checked statically only, and the new `FluidNormal`
texture (t11) and `SurfelBits` buffers are unverified on the Windows backends (§7).

Work orders: [backyard8-surfel-work-orders.md](backyard8-surfel-work-orders.md). Pipeline
reference: [backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md).

Branch `backyard8-surfels` from `0a1852a`. Commits: WO-1 `3c4d2d1` + `87add7f` (engine
plumbing), WO-2 `7df5fcb` + `68904bd` (shaders), WO-3 `1721971` (scene, config, Dev GUI),
WO-6 `be6011b` + `f3ddff9` (surfel normals, round 2), WO-7 `92cbb2a` (smoothing setting);
references re-frozen in `b3ef0c7`. WO-4 and WO-8 changed no source.

## 1. What was wrong

The particle renderer's water style (renderMode 0, blend 1, style 0) shaded the summed
thickness as a fake height field: the normals faced the camera, there was no real
reflection, and the refraction was a thickness-scaled offset. It cost 2.17 ms of fluid
passes at 1080p on the water start, against 1.32 ms for screen space measured in the same
session (`0a1852a`).

Round 2: the first surfel implementation drew every particle as its own dome, because the
per-pixel ellipsoid normals were used directly. The calm pool read as a lumpy carpet with
no clean reflection.

## 2. The design

Surfel splatting feeds the existing water shade (`CS_ParticleShade`, `Mode = 2`):

- **Disc axis.** The per-pixel quadric normal is replaced by a per-particle disc axis,
  computed from 6 steps of power iteration on the view-space quadric matrix, starting from
  world up in view space. It converges to the largest eigenvalue's eigenvector, the
  disc's shortest axis. `flatness = 1 - saturate(shape2.w)` (the anisotropy's spray
  flag); the per-pixel normal is `normalize(lerp(quadric, axis, flatness))` — isolated
  droplets keep their dome, everything else is a flat disc.
- **13-tap gather.** `SampleFluidNormal` in `CS_ParticleShade` gathers the blended normal
  over a disc: the centre, a ring of 6 at half the radius, and a ring of 6 at the radius
  rotated by 30 degrees. Taps keep `SampleFluid`'s layer test against the centre depth;
  the weight is `exp(-2 r^2) * confidence`. The radius is
  `ShadeParams.y * Focal()/z * EnvParams.y` in particle radii, clamped to 1..48 px.
- **Mode 2.** The shade replaces the depth derivative's macro normal with the gathered
  surfel normal, lerped by `saturate(conf * 4)`. Everything after the macro normal is
  unchanged.

The particle renderer's water style runs the screen-space pipeline with a different
surface reconstruction: the scene maps (renderMode 0, blend 1, style 0) to a descriptor
with `render_mode = ScreenSpace` and `screen_space.surfels = true`. Debug styles 1-3 keep
the `Fluid Sphere Shade` path unchanged. Anisotropy and whitewater run for both water
renderers.

## 3. Data flow

Pass order in `Assets/Scenes/Backyard8Scene_RenderGraph.json`:

```
Fluid Splat (10)          CS_FluidSplat stage 0   front-most ellipsoid depth per pixel
Fluid Surfel Splat (11)   CS_FluidSplat stage 2   scatters every particle again; a
                                                  ray-ellipsoid hit within SurfelBand
                                                  metres behind the front depth is
                                                  blended into five per-pixel sums
                                                  (weight, weighted depth offset,
                                                  weighted view-space normal) in
                                                  __fluid_surfel_bits_BackyardWater
Fluid Resolve (15)        CS_FluidResolve stage 0   screen space only: disabled for surfels
Fluid Resolve (surfels)   CS_FluidResolve stage 2 → FluidDepthA (blended depth),
   (16, stage 2)                                  FluidNormal (RGBA16F, $window_half),
                                                  FluidThickness; clears the sums
Fluid Surface Shade (26)  CS_ParticleShade Mode 2 reads FluidNormal (t11);
                                                  13-tap disc gather
```

New targets: `FluidNormal` (RGBA16F storage, `$window_half`); compute buffer
`__fluid_surfel_bits_BackyardWater` (stride 4, `element_count` 5, `$window`, `clear`
true — zeroed at creation, the resolve clears it every frame). Both surfel passes reuse
`CS_FluidSplat.hlsl` / `CS_FluidResolve.hlsl` with `compute_stage` 2.

## 4. Config keys

Keys in `Assets/Scenes/Backyard8Fluid.json`, `particles` section; the JSON values equal
the C++ defaults in `ParticleSystem.h` (the schema rejects unknown keys).

| Key | Default | Range | What it does |
|---|---|---|---|
| `surfelBand` | 1.0 | [0.1, 8] | Front-layer depth, in particle radii; a ray-ellipsoid hit within this band behind the front depth is blended |
| `surfelSharpness` | 2.0 | [0.25, 8] | Exponent on the ellipsoid's centre weight |
| `surfelSmoothing` | 1.0 | [0, 4] | Normal gather radius in the shade, in particle radii |

Dev GUI: the "Surfels" section with sliders `surfel band (radii)` 0.1..8, `surfel
sharpness` 0.25..8 and `normal smoothing (radii)` 0..4 (all `lookChanged`). The Fluid
Simulation renderer combo reads "particles (surfel splatting)" / "screen-space fluid
(smoothed depth)".

## 5. Cost

Fluid-pass GPU at 1080p, second of two 400-frame runs, water start:

| Renderer | Water start | Lake start |
|---|---|---|
| Surfels | 1.84 ms (Surfel Splat 0.57-0.58 ms) | 1.19 ms |
| Screen space | 1.21 ms | 0.79 ms |
| Old splat water style (`0a1852a`) | 2.17 ms | not measured |

The lake column for the old splat water style is not measured. The old splat row comes from
an earlier session, in which screen space measured 1.32 ms; the surfel and screen-space rows
were measured together. Compare rows of the same session.

## 6. Verification

Vulkan-only on native Linux.

- **Gate** (Claude's `b3ef0c7`, full gate PASS on HEAD): screen-space 0.002/0.077/0.033
  %, splat 0.000/0.060/0.247 %, spheres 0.001/0.456/0.003 %, lake -0.0434 m, water
  -0.1700 m.
- **`splat` references re-frozen** from three-run means, spread 0.002 / 0.056 / 0.214 %;
  f300 bbox top 229 (the far wet-deck patch).
- **Self-test:** 78 PASS, 0 FAIL.
- **Prototype vs committed renders:** mean absolute difference 0.61 / 0.09 / 0.27 levels
  (lake / lake normals / high view). On the splash: 4.33, against 4.38 between two runs
  of the same shaders.
- **Calm-lake normal-view green standard deviation:** 1.07 (committed) vs mottled before.

## 7. What is not done

1. **D3D11/D3D12/GL not run.** The new `FluidNormal` texture (t11) and the `SurfelBits`
   buffers are unverified on the Windows backends; the GL twins are checked statically
   only.
2. **The `exit(0)` exit fault.** `DayScene.cpp:2601` calls `exit(0)` straight after the
   frame dump (code from 2026-05-26, not this branch). Pool, Jolt and driver threads
   are still alive and GPU work may still be in flight, so static destructors race
   them. It fits 2 bad exits in about 20 runs under concurrent load, against 0 in 62
   other runs; it affects only `--dump-frame` runs. Open item: request a clean quit
   instead of `exit(0)`. Not fixed on this branch.
3. **The surfel pass costs 0.5-0.6 ms more** than the smoothing it replaces, mostly the
   scatter loop of front-layer particles.
4. **The slider's small effect.** The `surfelSmoothing` slider costs 0.01 ms; the
   measured effect is small — normal-map roughness on the splash at f90 (mean
   |Laplacian| of debug view 1) is 4.23 at 0 and 4.15 at 4, and the two runs agree
   within 0.02. On calm water every disc already points up.
