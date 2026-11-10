# Backyard Scene 8: Fluid Rendering Fixes and the D3D11/D3D12 Port

Status: **Vulkan on Linux** (RTX PRO 6000) and, since 2026-11-09, **D3D12, D3D11 and GL
natively on Windows** (RTX 4080 Laptop) are verified. **Linux GL is not supported by the current
Linux build** (Vulkan-only), although the OS driver is present. Sections 5 and 6 were executed;
their results are in section 10.

Update (same day): the particle renderer's default style is now **water** (particles blended
into water through their thickness, section 2.2); the lit spheres became the **spheres** debug
style (`particles.style: 3`). `CS_FluidSphereShade` gained constants and bindings (section 4).
The solver's seed is no longer a cubic lattice (section 2.5), so every reference number in
section 7 was re-measured.

Related: [diagnosis-3](diagnosis-3.md) (plan), [diagnosis-3-implementation](diagnosis-3-implementation.md)
(previous state), [backyard8-screen-space-fluid](backyard8-screen-space-fluid.md) (pipeline).

## 1. What was wrong after diagnosis-3

Both renderers were checked on Vulkan at frames 24 (sphere in the air), 45/90/120 (impact and
splash) and 300/360 (settled pool), at 960x540 and 1920x1080.

| Path | Symptom | Cause |
|---|---|---|
| Screen space | Lumpy "cellophane" water with dark seams and glassy ridges along every overlap | `CS_ParticleSmooth` was a depth-blind Gaussian: it averaged the falling sphere into the pool behind it and splash sheets into each other. Only passes 1 and 2 could ever filter (stage 3+ copied). |
| Screen space | Ghostly water; thin water, spray and edges half transparent | Alpha came from thickness (`coverage * 4`), so the background was blended in a second time wherever the water was thin. |
| Screen space | Settled pool had no visible surface | "Reflection" sampled the scene a few pixels away from the pixel; it was another refraction sample, not a reflection. |
| Screen space | Refraction bent the wrong way and pulled the coping and posts into the water | The F9 change mirrored the offset; nothing rejected samples of geometry in front of the water. |
| Screen space | Radial streaks at silhouettes; everything through 4 m of water saturated cyan | Raw chord-sum moire; absorption multiplier 4. |
| Particles | A flat, unlit, translucent blob; no individual particles, no depth order | The splat resolve averaged every overlapping disc into one colour, and looped over every particle for every covered pixel. |
| Particles (first fix) | Lit spheres: clear particles, but beads rather than water | Each pixel showed one opaque particle; no blending between particles, no absorption, refraction or reflection. |
| Both, frames 0 to 40 | Crystal rows on the falling sphere: terraces and a serrated lower edge (screen space), grid speckle and moire (water) | The solver seeded a perfect cubic lattice, whose rows and planes line up with the camera until the impact breaks them. |
| Validation | `check_fluid_constants.py` failed | It could not size `float3`/`float4` cbuffer members of the new kernels. |

## 2. What changed

### 2.1 Screen-space path

| Stage | Kernel | Change |
|---|---|---|
| Clear | `CS_FluidClear` | Clears the depth buffer to `0xFFFFFFFF`. One sentinel now serves both the float depth bits and the particle key (2.2). One thread per pixel on a width x height grid of 8x8 groups: the graph dispatches `"$window"` buffers in 2D, because a 1D grid needed 129,600 groups at 3840x2160 (limit 65,535). |
| Scatter | `CS_FluidSplat` | Unchanged in screen-space mode (`ScreenSpace = 1`). |
| Resolve | `CS_FluidResolve` | Empty = any non-finite bit pattern. 5x5 binomial blur of the thickness (removes lattice moire). Coverage is now an anti-alias term: `smoothstep(0, 0.7 * Radius * InvPackingFactor, thickness)`, so it reaches 1 after about a third of one particle's chord. |
| Smooth | `CS_ParticleSmooth` | Edge-aware **a-trous** filter: each pass is a 5x5 B3-spline whose taps are `Spacing` pixels apart. The graph's passes use spacing 1, 2, 4, 8, 16 and a final spacing-1 cleanup (6 passes). Each pixel stops once the spacing exceeds half its projected radius `WorldRadius * (H/2) * P11 / z` (capped at `RadiusPx`), so the filter has a fixed **world** size. A neighbour more than `Tolerance` metres nearer or farther is another layer and is ignored. Empty pixels are never written. |
| Shade | `CS_ParticleShade` | Normals: central differences inside the surface, one-sided only across a depth jump larger than two particle radii. Refraction: Snell (`refract`, eta 1/1.33), follow the ray `min(thickness * Refraction, 2 m)` and look the scene up there; reject the sample if the scene there is in front of the water. Reflection: screen-space ray march against `GBuffer:DEPTH` (64 geometric steps, interleaved-gradient jitter, 5-step bisection, edge fade); a miss uses the sky seen on screen along the ray, else the sky from the frame's top row, else `ReflectionColor`. Paper Eq. 13 with Schlick Fresnel. Spray: a pixel whose ring 2.5 particle radii out is mostly empty fades toward lit whitewater and loses 35% alpha (`foam`). Alpha = silhouette coverage only. |
| Composite | `PARTICLE_COMPOSITE` | Unchanged (straight alpha over `Deferred:COLOR0`). |

### 2.2 Particle path ("splat" renderer, water and spheres styles)

| Stage | Kernel | Change |
|---|---|---|
| Stamp | `CS_ParticleSplat` (stamp) | Unchanged: writes `__particle_projected_<system>` (`uv`, radius px, view depth). |
| Clear | `CS_FluidClear` | Same pass as screen space. |
| Scatter | `CS_FluidSplat` with `ScreenSpace = 0` | Per covered pixel, `InterlockedMin` of a packed key: 15 bits of log-spaced front depth (0.05..1000 m) above a 17-bit particle index. Also sums the chord thickness, exactly as in screen-space mode. Full particle radius for both. |
| Shade | **new** `CS_FluidSphereShade` | `Style` 0, **water** (default): thickness from a 5x5 binomial over the summed chords (taps a quarter of a particle radius apart), normal from the gradient of a dense 9x9 binomial over the same thickness (the thickness acts as twice the height of a surface facing the camera). Then the screen-space shade's optics: Snell refraction over `min(thickness * refraction, 2 m)` with occlusion reject, Beer-Lambert absorption toward the fluid colour, Schlick Fresnel toward the sky seen on screen (top row) or `SkyColor`, sun highlight. Alpha = `smoothstep(0, 0.7 * Radius * InvPackingFactor, thickness) * opacity`. `Style` 3, **spheres** (debug): decodes the nearest particle, rebuilds its sphere normal from the pixel offset inside its disc and lights it (sun diffuse + Blinn-Phong, sky/ground hemisphere, rim), ramp colour with the per-particle hash jitter, alpha = `opacity`. |

The water style takes its optics from `screenSpace.*` (absorption, absorption scale, fluid
colour, refraction, specular, Fresnel power, thickness scale, packing factor), so both renderers
draw the same water. The 9x9 normal kernel is deliberate: a sparse kernel with the same
footprint aliases the particles of the deeper layers into moire or ghost grids. Through deep
water the gradient is mostly the noise of many summed particles, so the refracted ray uses the
slope divided by `1 + thickness / (8 * Radius)`; glints and Fresnel keep the full normal.

The wireframe and flat debug styles, and additive blending, still use the legacy splat resolve.

### 2.3 Files

| File | Change |
|---|---|
| `T850/Assets/Shaders/CS_FluidClear.hlsl` | Sentinel; `[numthreads(8, 8, 1)]`, index `id.x + id.y * OutputWidth` |
| `T850/Assets/Shaders/CS_FluidSplat.hlsl` | Particle-key mode (also sums thickness) |
| `T850/Assets/Shaders/CS_FluidResolve.hlsl` | Empty test, thickness blur, coverage |
| `T850/Assets/Shaders/CS_ParticleSmooth.hlsl` | A-trous filter; cbuffer `Stage` became `float Spacing`, `Pad0` became `WorldRadius` |
| `T850/Assets/Shaders/CS_ParticleShade.hlsl` | New shading; cbuffer reshaped to 52 DWORDs; new binding `t3` scene depth |
| `T850/Assets/Shaders/CS_FluidSphereShade.hlsl` | New kernel: water and spheres styles |
| `T850/Assets/Shaders/*.glsl` (the six above) | Regenerated GL twins (section 6.3) |
| `T850/Assets/Shaders/compute_kernels.json` | `particle_shade`: 52 constants + `t3`; new `particle_sphere_shade` (56 constants, section 4) |
| `T850/Assets/Scenes/Backyard8Scene_RenderGraph.json` | New pass `Fluid Sphere Shade` (keys, thickness, `Deferred:COLOR0` + sampler at register 3, `GBuffer:DEPTH` at `t4`); `GBuffer:DEPTH` bound to `Fluid Surface Shade` at `t3` |
| `T850/Assets/Scenes/Backyard8Fluid.json` | New defaults (section 2.4) |
| `T850/Framework/src/utils/ComputeKernelRegistry.cpp` | Smooth/shade constant layouts, `particle_sphere` builder (style, optics), shared `SunInView` |
| `T850/Framework/include/scene/ParticleSystem.h`, `T850/Framework/src/scene/ParticleSystem.cpp` | `smoothing_world_radius`, `reflection_distance`; validation ranges; `ParticleStyle::Spheres` |
| `T850/DayScene/Backyard8Scene.cpp`, `T850/DayScene/Backyard8FluidJson.h` | Pass gating (section 3), config mapping, start modes, Dev GUI controls (all apply at once; seeding controls re-seed), style 0..3 |
| `T850/Framework/src/core/LinuxFramework.cpp` | Linux only: windows centred on the primary display (honoured under X11/XWayland; native Wayland places windows itself) |
| `T850/Framework/src/scene/RenderGraph.cpp`, `T850/Framework/include/scene/RenderGraph.h` | A pass whose extent is a `"$window"` buffer dispatches width x height, not a 1D grid (all backends) |
| `T850/Framework/src/simulation/PbfFluid.cpp`, `T850/Framework/include/simulation/PbfFluid.h` | Resting pool seed, relaxed sphere, `Settle` (section 2.5); CPU only, no shader or ABI change |
| `T850/Framework/include/scene/ParticleSystem.h` (`buffer_capacity`), `T850/Framework/src/scene/RenderGraph.cpp` | The graph sizes a particle system's projected table for `buffer_capacity` (scene 8: 131,072, the scatter key's index range), so the particle count can change at runtime |
| `T850/scripts/validation/check_fluid_constants.py` | HLSL vector/array packing; also scans `CS_Particle*.hlsl` |

### 2.4 Shipped settings (`Backyard8Fluid.json`)

| Key | Old | New | Meaning now |
|---|---|---|---|
| `particles.opacity` | 0.82 | 1.0 | Water / sphere alpha |
| `particles.style` | 0 | 0 | 0 water (blended), 1 wireframe, 2 flat, 3 spheres (lit, debug) |
| `screenSpace.smoothingIterations` | 2 | 6 | A-trous passes (spacing 1, 2, 4, 8, 16, cleanup 1) |
| `screenSpace.smoothingRadius` | 24 | 64 | Cap on the projected filter radius, px |
| `screenSpace.smoothingWorldRadius` | - | 0.3 | Filter radius, metres (new key) |
| `screenSpace.depthTolerance` | 0.3 (unused) | 0.3 | Layer threshold, metres |
| `screenSpace.absorptionScale` | 4.0 | 2.0 | Multiplier on per-metre absorption |
| `screenSpace.refraction` | 0.02 (UV) | 0.5 | Refracted path per metre of thickness |
| `screenSpace.reflectionDistance` | - | 40 | Reflection march range, metres; 0 = sky only (new key) |
| `screenSpace.normalStep` | 2 | 1 | Normal stencil, px |
| `screenSpace.foam` | 0 | 0.6 | Spray whitewater, 0..1 |

### 2.5 Seed (solver, CPU)

`PbfFluid::SeedParticles` seeds a resting pool (a cell-centred lattice in the basin, half a
spacing off every face, its top layer partly filled so the volume follows the level exactly)
and, for the water start, the falling sphere above it. Start modes (`scene.startMode`,
`--backyard-start`):

| Start | Seed | Particles | Settles at |
|---|---|---|---|
| `lake` (2, default) and `balls` (0) | Pool to `restingLevel` (+0.16 m), settled before the first frame | 29,880 | -0.04 m, scene 7's water plane |
| `water` (1) | Pool to `prefillLevel` (+0.13 m) and the 2 m sphere | 44,573 (a third splashes over the rim and is culled) | -0.04 m |

Both levels sit above the plane on purpose: the seeded lattice compacts by about one particle
layer (~16 cm) as it settles, and a pool seeded exactly at the plane (26,993 particles) settles
that much low. The resting starts run 3 s of simulation (speeds capped at 0.3 m/s, no wind,
about 200 ms at load) before the first frame, so the compaction is never shown.

The sphere is jittered by up to 0.4 spacing per axis (a hash of its index, so it is
reproducible) and relaxed with the solver's own one-sided density projection on the CPU (same
normalised Poly6/Spiky kernels, CFM and 0.1-spacing step cap) to 0.5% below rest density: 26
iterations and about 27 ms, maximum compression 0.573 to 0.0019. The GPU constraint is zero
during the fall, so the sphere falls without a pop and without lattice rows, terraces or a
serrated edge. Nothing in the backends changes.

## 3. Pass gating

`Backyard8Scene::ApplyRenderModeToGraph` enables passes per mode; the graph is never rebuilt.
"Particles" means render mode splat, style water (0) or spheres (3), blend alpha; the kernel
picks the style from its `Style` constant.

| Pass | Screen space | Particles | Wireframe / flat / additive | Screen space + `splatFallback` |
|---|---|---|---|---|
| `Fluid Particles (stamp)` | on | on | on | on |
| `Fluid Particles (resolve)` | off | off | on | on |
| `Fluid Clear` | on | on | off | on |
| `Fluid Splat` | on (surface) | on (key) | off | on |
| `Fluid Sphere Shade` | off | on | off | off |
| `Fluid Resolve` | on | off | off | on |
| `Fluid Smooth 1..6` | on | off | off | on |
| `Fluid Surface Shade` | on | off | off | on |
| `Fluid Particle Composite` | on | on | on | on |

`Fluid Clear` and `Fluid Splat` read the screen size from their pass target (`FluidDepthA`)
while dispatching over a buffer: `Fluid Clear` over its `"$window"` depth buffer as a
width x height grid, `Fluid Splat` over the projected particle table as a 1D grid.
`Fluid Sphere Shade` writes every pixel of `FluidParticles`.

## 4. Kernel ABI

Every backend must agree with these bindings. "Root" is the cost the registry computes against
D3D12's 64-DWORD root signature; above 64 the cbuffer is bound as a root CBV (already
supported, logged at INFO).

| Kernel | b0 DWORDs | SRVs | UAVs | Sampler | Root |
|---|---|---|---|---|---|
| `CS_FluidClear` | 64 `ParticleConstants` | - | `u0` depth bits `uint`, `u1` thickness `uint` | - | 68 (root CBV) |
| `CS_FluidSplat` | 64 | `t0` projected `float4`, `t1` scene depth `Texture2D<float>` | `u0`, `u1` (atomics) | - | 71 (root CBV) |
| `CS_FluidResolve` | 64 | `t0` depth bits `uint`, `t1` thickness `uint` | `u0` `FluidDepthA` r32f, `u1` `FluidThickness` rgba16f | - | 70 (root CBV) |
| `CS_ParticleSmooth` | 8 | `t0` `Texture2D<float>` | `u0` r32f | - | 10 |
| `CS_ParticleShade` | **52** | `t0` depth, `t1` thickness, `t2` scene colour, **`t3` scene depth** | `u0` rgba16f | `s2` | 58 |
| `CS_FluidSphereShade` | **56** | `t0` projected `float4`, `t1` keys `uint`, **`t2` thickness bits `uint`**, **`t3` scene colour**, **`t4` scene depth** | `u0` rgba16f | **`s3`** | 66 (root CBV) |

Buffer strides match the element types (D3D11 builds views from the creation stride):
`__particle_projected_*` 16 bytes, `__fluid_*_bits_*` 4 bytes.

The host structs are in `ComputeKernelRegistry.cpp` with `static_assert` sizes 32
(smooth), 208 (shade), 224 (sphere), 256 (splat/clear/resolve).

The render graph pairs a `sampler` resource with the `sampled` resource of the **same
register**, so the sphere kernel's sampler is `s3` next to `t3`; a mismatch fails the graph load
(`sampler 'Deferred:0' has no matching sampled resource`).

`CS_FluidSphereShade` constants, in order (DWORD offsets): `OutputWidth, OutputHeight,
ParticleCount, Style` (0); `Radius, Opacity, ProjScaleY, Ambient` (4); `ProjA, ProjB,
Refraction, Specular` (8); `FresnelPower, ThicknessScale, InvPackingFactor, Pad0` (12);
`SunDirection` (16), `SunColor` (20), `SkyColor` (24), `WorldUp` (28), `FluidTint` (32),
`FluidColor` (36), `Color0..3` (40..52).

## 5. Port to D3D12 (do this first)

> **Done 2026-11-09 (native Windows):** Release x64 build succeeded; all nine captures pass;
> 4K clears 480 x 270; selftest 78 PASS. See section 10.

D3D12 matched Vulkan before this change (diagnosis-3 E2), and nothing here is
D3D12-specific, so this should be a build-and-verify step.

1. On the Windows machine, from `T850/`: `.\scripts\build.ps1 -Config Release -Platform x64 -Action Build`.
   Expect `BUILD SUCCEEDED`. No `.vcxproj` change is needed (no new C++ files).
2. From `T850\bin\x64\Release`, run the reference captures of section 7 with `--api d3d12`.
3. In each log, confirm for every fluid pass: `enabled compute kernel` and `dispatched compute`,
   and none of `Shader compile failed`, `using graphics implementation`,
   `compute dispatch failed`, `DispatchTooLarge`, `cannot build constants`,
   `does not declare the kernel's complete binding layout`.
4. Compare with the Vulkan references (section 7).
5. Run `.\DayScene.exe --unattended --api d3d12 --game-selftest`: 78 PASS, exit 0.

If `Fluid Surface Shade` or `Fluid Sphere Shade` fails to create its pipeline, check the root
signature first (the shade is under 64; the sphere shade needs 66 and binds its cbuffer as a
root CBV, logged at INFO) and that the new `t3` (shade) and `t1`..`t4` (sphere) SRVs and the
sphere's `s3` sampler get descriptor-table or root-descriptor slots like the other kernels.

## 6. Port to D3D11

> **Done 2026-11-09 (native Windows):** FXC `cs_5_0` compiled all ten PBF pipelines, including
> the new `SolidBoxes` bindings on the density and update-velocity kernels. No X3511, compile
> failure or graphics fallback; captures pass. See section 10.

### 6.1 Compiler rules (FXC, `cs_5_0`)

`D3D11Compute.cpp` compiles every kernel with `D3DCompile(..., "cs_5_0")`. The six kernels
were written for it, but nobody has compiled them with FXC yet:

- No `Sample` in compute (implicit derivatives); only `SampleLevel` and `Load` are used.
- No local variables named after intrinsics; `ddx`, `ddy` and `step` in the shade were renamed.
- `InterlockedMin`/`InterlockedAdd` are on `RWStructuredBuffer<uint>` only (supported in `cs_5_0`).
- Loops: `[loop]` for the reflection march and the a-trous taps are fixed-size `[unroll]`.
  The sphere shade's 9x9 normal kernel is a fully unrolled 81-tap loop over `static const`
  weight arrays.
  If FXC reports X3511 (unable to unroll loop), change the offending `[unroll]` to `[loop]`;
  do not restructure the maths.
- If FXC reports a warning promoted to an error, fix it in the HLSL, then regenerate the GL
  twin (6.3).

### 6.2 Run

1. Build as in section 5.1.
2. Run the captures with `--api d3d11`; check the same log lines as 5.3.
3. D3D11 unbinds every SRV/UAV after each dispatch (`D3D11Compute.cpp`), which the
   UAV-then-SRV use of `__fluid_depth_bits_*` relies on. With the debug layer on, there must
   be no `Resource being set to CS shader resource slot ... is still bound on output` warnings.
4. Self-test as in 5.5 with `--api d3d11`.

**Proton:** D3D11 results under Proton are only valid with Microsoft's `d3dcompiler_47.dll`
(`WINEDLLOVERRIDES="d3dcompiler_47=n"` with the SDK redistributable next to `DayScene.exe`).
Wine's builtin compiler miscompiles these loop kernels; see [diagnosis-3](diagnosis-3.md) F2.
D3D12 under Proton is valid (DXC + vkd3d-proton).

### 6.3 GL twins

The six GL twins were regenerated on Linux with the documented pipeline
(`glslangValidator -D -S comp -e CS --target-env vulkan1.0 -DT850_SPIRV=1 -DT850_VULKAN=1
-DT850_GL=1`, then `spirv-cross --version 430 --no-es --combined-samplers-inherit-bindings`,
banner with the HLSL SHA-256, CRLF). If any HLSL changes during the port, regenerate with
`scripts/shaders/generate_gl_compute_twins.ps1 -Kernel <stem> -Force` and run
`python scripts/validation/check_gl_twins.py --shaders Assets/Shaders`.

Windows GL has been run with these changes (native OpenGL 4.6, NVIDIA 576.52): all nine
captures, 4K and the selftest pass. **Linux GL is not supported by the current Linux build**:
`Framework/CMakeLists.txt` compiles `src/video/gl/*` only under `WIN32`, and
`LinuxFramework.cpp` always creates `VulkanDriver`, so `--api gl` falls back to Vulkan. The
Linux OS driver itself is present (EGL probe: NVIDIA RTX PRO 6000, OpenGL 4.6, driver
615.71.09). Enabling Linux GL needs a desktop GLX/EGL context plus GL driver selection in
`LinuxFramework`; the existing `GLDriver.cpp` `OS_LINUX` path is GBM/EGL GLES3 surfaceless,
not the GL 4.3 path the GLSL-430 twins require. That is a separate port, not a build flag.

## 7. Verification protocol

Captures (PowerShell; on Linux use `./DayScene` from `bin/SteamDeck/Release` with
`LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` and `--api vulkan`). `splat` draws the default water
style; for the spheres style run the splat loop again with `--fluid-config <abs path>` pointing
at a copy of `Assets/Scenes/Backyard8Fluid.json` with `"style": 3` and `"renderMode": 0`:

```powershell
$api = 'd3d12'   # then d3d11
foreach ($mode in 'screen-space', 'splat') {
  foreach ($frame in 24, 90, 300) {
    .\DayScene.exe --unattended --api $api --scene 8 --backyard-no-hud --backyard-start water `
      --width 960 --height 540 --regressionFixedDt 0.0166667 --postProcessMode compute `
      --fluid-render $mode --dump-frame $frame --logLevel info --logFile "logs\$api-$mode-f$frame.log"
  }
}
```

`--backyard-start water` is the drop (the default start is the resting pool, which exercises
less of the renderer).

Each run writes `dumps_<api>_f<frame>_<time>\RT_Dump_FluidParticles.ppm` (the fluid layer
before compositing) and `RT_Dump_BackBuffer.ppm`. Statistics over non-black pixels of the fluid
layer:

```python
import sys, numpy as np
from PIL import Image
a = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(float)
m = a.sum(axis=2) > 0; ys, xs = np.nonzero(m); mean = a[m].mean(axis=0)
print(f'px={m.sum()} bbox=x[{xs.min()},{xs.max()}] y[{ys.min()},{ys.max()}] '
      f'mean=({mean[0]:.1f},{mean[1]:.1f},{mean[2]:.1f})')
```

Vulkan references (960x540, shipped config, `--backyard-start water`):

| Mode | Frame | Pixels | Bounding box | Mean RGB |
|---|---|---|---|---|
| screen space | 24 | 219,014 | x[0,959] y[0,436] | (71.2, 140.6, 150.2) |
| screen space | 90 | 223,509 | x[0,959] y[97,443] | (72.9, 126.2, 138.7) |
| screen space | 300 | 126,790 | x[0,959] y[278,440] | (69.8, 136.3, 155.4) |
| water | 24 | 219,007 | x[0,959] y[0,436] | (63.4, 132.6, 144.1) |
| water | 90 | 223,708 | x[0,959] y[81,440] | (71.9, 121.5, 133.5) |
| water | 300 | 127,214 | x[0,959] y[279,440] | (57.8, 130.5, 149.0) |
| spheres | 24 | 219,011 | x[0,959] y[0,436] | (213.8, 235.7, 237.8) |
| spheres | 90 | 224,870 | x[0,959] y[99,440] | (224.7, 243.0, 244.9) |
| spheres | 300 | 126,457 | x[0,959] y[278,439] | (239.6, 253.9, 254.5) |

Re-measured after the Phase 1 solver changes (`restDensityScale 0.907`, `wallFriction 8`,
settle-before-drop, `prefillLevel -0.27`). The previous table (228,390 px at frame 24) is
obsolete.

The sphere means are high because the layer is HDR and the dump clamps it.

Pass criteria per backend:

- **Frame 24** (free fall): pixel count within 0.5%, bounding box within 2 px, each mean
  channel within 3 levels. Better: `python scripts/compare_dumps.py <vulkan dump dir>
  <candidate dump dir> --tolerance 8` with under 1% of fluid pixels over tolerance.
  **Vulkan frame 24 is not bit-deterministic.** Measured after Phase 1: 4,915 of 518,400 fluid
  pixels differ between two runs (max 171 levels); the pre-Phase-1 config differed on 9,255
  (max 218). The wind gate is now integer, but the neighbour grid's atomic insertion order
  changes the density summation. Compare statistics, not pixel identity; bit-exactness would
  need deterministic ordering inside each grid cell.
- **Frames 90 and 300**: the solver is not bit-deterministic after impact (GPU atomics order;
  two Vulkan runs differ by about 6 levels mean absolute per pixel), so compare statistics only:
  pixel count within 2%, mean within 5 levels. The **frame-90 splash bbox is not a stable
  statistic**: three Vulkan runs gave y_min 81, 115 and 115 (a 34 px spread), the Windows D3D
  runs gave 112-113, and GL gave 90-101. Accept a frame-90 bbox difference up to ~40 px and
  confirm the look;
  frame 300 bbox is stable (0-1 px).
- **Normal signs** (screen space, frame 24, `screenSpace.debugView: 1` in a copy of the config,
  passed with `--fluid-config <abs path>`): run the quadrant test from
  [diagnosis-3 section 9](diagnosis-3.md#9-verification-helpers). Vulkan: left -0.51 / 99%,
  right +0.50 / 98%, bottom -0.64 / 98% correct sign.
- **Look**: frame 24 is a clear, refracting ball with sky reflection on top above a full pool,
  in both screen space and water, with no rows, terraces or serrated edge on the ball; frame 90
  is the splash, with walls of water thrown over the long sides; frame 300 a full, rippled pool
  at the coping. The water style is transparent and tinted, with sun glints and the scene
  refracted through it; the spheres style shows randomly packed lit spheres in depth order. No
  vertical streaks, no grey discs, no dark seams, no moire.

Performance reference (Vulkan, RTX PRO 6000, 1920x1080, whole frame, unpaced
`--benchmarkFixedDt 0.0166667`; wall time of a `--dump-frame 700` run minus a `--dump-frame 100`
run, divided by 600, screen space): resting pool 2.7 ms (29,880 particles), drop 3.2 ms
(44,573 seeded). Diagnosis-3 measured 15 ms and 8 ms for the old paths. A D3D backend more than
about twice the Vulkan cost points at a missed fast path (for example the legacy splat resolve
still enabled).

Linux window placement: windows are created centred on the primary display. Native Wayland
ignores that (GNOME opens the window under the pointer), so automated captures run with
`SDL_VIDEO_DRIVER=x11`. Do not use XWayland for viewing: at 1.25 scaling GNOME shows XWayland
windows at 0.625 of their pixel size, so the window and ImGui look about half as large (see
`T850/BUILD-UBUNTU.md`). Windows builds are unchanged.

Also capture one frame at 3840x2160 per backend: `Fluid Clear` must log `dispatched compute
480 x 270 x 1` and no `DispatchTooLarge`.

## 8. Troubleshooting

| Symptom | Likely cause | Check |
|---|---|---|
| Screen-space water missing, splat blob visible | Surface Shade pipeline failed; fallback | Log for `Fluid Surface Shade`; FXC errors (6.1); root signature (5) |
| Water drawn over the coping/posts | Scene depth not bound or wrong linearisation | `t3`/`t1` bound to `GBuffer:DEPTH`; `ProjA/ProjB` from `Projection.m[2][2]/m[3][2]` |
| No reflections, uniform tint | Reflection march never hits | `reflectionDistance > 0`; scene depth readable at `t3` |
| Spheres mode empty | Key buffer read as zeros or sentinel | `Fluid Clear` → `Fluid Splat` → `Fluid Sphere Shade` order; UAV-to-SRV transition of `__fluid_depth_bits_*` |
| `[DispatchTooLarge] pass='Fluid Clear'` at 4K | 1D dispatch over the per-pixel buffer | Graph dispatches `"$window"` buffers as width x height; clear kernel is `[numthreads(8, 8, 1)]` |
| Config rejected at 1:1 `expected_brace` | UTF-8 BOM in the `--fluid-config` JSON | Save UTF-8 without BOM; glaze rejects the BOM |
| Linux `--api gl` renders with Vulkan | Linux build has no GL backend | Expected; see 6.3. Do not count it as a GL pass. |
| Spheres in the wrong order or flickering | Key packing differs | 17 index bits and the log2 depth constants identical in `CS_FluidSplat` and `CS_FluidSphereShade` |
| Water style invisible or a uniform flat tint | Thickness not summed or not bound | `CS_FluidSplat` adds thickness in both modes; `__fluid_thickness_bits_*` at `t2`; UAV-to-SRV transition |
| Water style: radial spikes or stripes inside the falling sphere | Normal kernel aliasing | Normal from the dense 9x9 `ThicknessGradient9`, not the sparse 5x5 field |
| Graph fails to load: `sampler ... has no matching sampled resource` | Sampler register differs from its texture | Sphere shade uses `s3` with `t3` in HLSL, manifest and graph |
| Water upside down or mirrored (GL only) | Missing image-Y flip | Twin generated with `-DT850_GL=1` |
| Blocky or striped normals | Filter differs | `Spacing` is 1, 2, 4, 8, 16, 1 per pass (host `ParticleSmoothConstants`) |
| `cannot build constants` | Host struct and `constant_count` disagree | 52 (shade), 56 (sphere), 8 (smooth); `check_fluid_constants.py` |

## 9. Known limitations (not regressions)

- The water start throws about a third of its particles over the rim, where they are culled;
  its pool is seeded higher to compensate. The pool under the drop is not settled first, so
  for the first half second it shows lattice rows along the near wall.
- The pool keeps moving: wind (8 m/s) drives ripples and a slow slosh of a few centimetres.
- The water style has no front-surface reconstruction: it reads the surface from the
  thickness, so it cannot show reflections of the scene (sky only) and its normals are softer
  than screen space. Use screen space for the final look.
- The water normal reads 81 thickness values per fluid pixel; free here, but measure it on
  low-end GPUs.
- Silhouettes are scalloped at particle scale (0.13 m particles); spray droplets render as soft
  whitewater discs.
- Screen-space reflections only see what is on screen; off-screen reflections use the sky.
- No half-resolution path, no IBL cubemap in the compute shade, no exact curvature flow
  (diagnosis-3 phases 4 and 6 remain).
- The legacy splat resolve (wireframe/flat debug styles, additive) still loops over every
  particle per covered pixel.

## 10. Verification done on Vulkan and Windows (2026-11-09)

| Check | Result |
|---|---|
| Linux build (`cmake --build build/linux --target DayScene`) | pass |
| `DayScene --unattended --api vulkan --game-selftest` | 78 PASS, exit 0 |
| `check_gl_twins.py` | PASS, 53 kernels, 48 generated twins |
| `check_fluid_constants.py` | PASS, 10 cbuffer names, 35 constant counts |
| Six regenerated twins, `glslangValidator -S comp` | 6/6 clean |
| Screen space and spheres, 960x540 and 1920x1080, frames 24/45/90/120/300/360 | visually correct; references in section 7 |
| Water style, 960x540, frames 24/90/300; spheres style re-checked at 24/90/300 | visually correct; references in section 7 |
| Relaxed seed, all three styles at frames 24/90/300 | no lattice rows; references re-measured (section 7) |
| Test windows on Linux (`SDL_VIDEO_DRIVER=x11`) | centred on the primary (4K) display |
| 3840x2160, screen space and water, frame 90 | `Fluid Clear` 480 x 270 groups, no errors (was `DispatchTooLarge`) |
| Wireframe and flat debug styles | still route through the legacy resolve |
| Phase 1: lake level, 1800 frames | −0.043 m (target −0.040 ± 0.010) |
| Phase 1: water level, 1800 frames | −0.0406 m (settle-before-drop, `prefillLevel -0.27`) |
| Phase 1: water frame 6 | settled pool, no lattice rows |
| Phase 1: two frame-24 runs | not identical (4,915 px, max 171); grid atomic order |
| Windows Release x64 build (native) | BUILD SUCCEEDED (02:55); Dawn metadata refreshed |
| D3D12 screen space / water / spheres, frames 24/90/300 | PASS: frame 24 within 0.01% px, 0 px bbox, 1.0 level; frames 90/300 within 0.5% px, 1.0 level; frame-90 bbox within Vulkan's own 34 px spread |
| D3D12 4K, frame 90 | `Fluid Clear` 480 x 270, no `DispatchTooLarge`, no errors |
| D3D12 selftest | 78 PASS, 0 FAIL |
| D3D11 screen space / water / spheres, frames 24/90/300 | PASS: frame 24 within 0.01% px, 0 px bbox, 1.3 levels; frames 90/300 within 0.9% px, 1.3 levels; frame-90 bbox within Vulkan's spread |
| D3D11 4K, frame 90 | `Fluid Clear` 480 x 270, no `DispatchTooLarge`, no errors |
| D3D11 selftest | 78 PASS, 0 FAIL |
| D3D11 FXC pipelines | 10/10 PBF pipelines created; no compile failure, no graphics fallback |
| Windows GL (native OpenGL 4.6, NVIDIA 576.52), 9 captures | PASS: frame 24 within 0.04% px, exact bbox, 1.9 levels; frames 90/300 within 0.72%, 2.6 levels; frame-90 bbox within Vulkan's spread |
| Windows GL compute pipelines | all PBF/fluid GLSL twins loaded and dispatched; no shader compile failure |
| Windows GL 4K, frame 90 | `Fluid Clear` 480 x 270, no `DispatchTooLarge` |
| Windows GL selftest | 78 PASS / 0 FAIL |
| Linux GL | OS driver present (NVIDIA OpenGL 4.6, 615.71.09); engine Linux build has no GL backend, `--api gl` falls back to Vulkan |

Windows frame-24 fluid layer at 960x540 (Vulkan reference in parentheses):

| API | Screen space | Water | Spheres |
|---|---|---|---|
| D3D12 | 219,018 (219,014) | 219,020 (219,007) | 219,022 (219,011) |
| D3D11 | 219,017 (219,014) | 219,022 (219,007) | 219,018 (219,011) |
| GL | 219,102 (219,014) | 219,095 (219,007) | 219,099 (219,011) |

All frame-24 bounding boxes are identical to Vulkan; mean-channel deltas are 0.04-1.24 levels.
