# Backyard 8: screen-space water realism fixes (2026-11-10)

Status: implemented and verified on **Vulkan (native Linux)**. D3D12, D3D11 and OpenGL were not
run (no Windows build of this tree on the verification machine); the HLSL follows the FXC
`cs_5_0` rules and every changed kernel's GL twin was regenerated and checked. Run the gate in
§5 on Windows before calling those backends done.

Starting point: [backyard8-session-2026-11-10.md](backyard8-session-2026-11-10.md) and the
screenshot `Screenshot From 2026-11-10 13-21-32.png` in this folder: dome-shaped blobs with a
crosshatch texture along the far coping and the near wall, and water that read as gel
("saliva") rather than pool water. Pipeline reference:
[backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md).

## 1. Root causes

| # | Symptom | Cause |
|---|---|---|
| R1 | Domes standing on the far coping and near wall | **Invisible wall in the solver.** `PbfFluid` used the layout's `freeboard` (0.42 m) as the wall height, so `WallTopY` was 0.38 m. `freeboard` is the VOF solver's air headroom (`PoolLayoutJson.h`), not geometry: `Backyard_Pool.glb` walls end at y 0.00 and the coping top is 0.08. Water climbed an invisible wall 30 cm above the coping; particle dumps showed wall-layer columns up to +0.23 m. Rendered, that water is a row of blobs floating over the rim. |
| R2 | Every free surface a field of domes; sheets a row of balls | **Isotropic spheres.** Each particle's depth was a sphere of radius = spacing. A flat surface of spheres is bumpy at the particle scale; the normals show every dome. |
| R3 | Crosshatch on the domes | The old SSR jittered its start with interleaved gradient noise; on curved domes the jitter became a visible hatch. |
| R4 | Bright lines along the near coping | The smoothing averaged one-sidedly at occlusion boundaries (only the water side was valid), biasing depth toward the camera; its range test compared against the centre depth, which rejects valid taps on a surface seen at a grazing angle (~45 mm of depth per pixel at 8 m). |
| R5 | Gel/"saliva" look | The shade had no water column: transmission used the noisy chord sum instead of the distance to the floor, so the pool read as a milky volume; reflections fell back to one flat sky colour; the surface was a perfect mirror (no sub-particle ripples); no caustics. |
| R6 | Jagged islands in the refraction | The refraction occlusion test compared the lookup's scene depth with **this** pixel's surface depth, so the pool floor nearer the camera (legitimately visible through the water) was rejected and fell back to the unrefracted pixel. |
| R7 | Hard-edged dark/bright patchwork in the reflection | Reflected rays that left the top of the frame snapped to a bright sky gradient although the house continues above the frame; every ripple flipped rays between "house" and "sky". Rays tilted back toward the camera "hit" the top of the near coping. |
| R8 | Dotted rows above the near coping | Refraction lookups whose bilinear footprint straddled the coping lip blended its dark edge in. |
| R9 | A white "snowman" on the top step | Two stranded particles (about 2 L of water each) drawn as 13 cm balls. At this resolution a resting lone particle is a film millimetres thick in reality. |

## 2. Fixes

| Area | Change | Files |
|---|---|---|
| Solver rim (R1) | `wallTopY = waterLine + copingHeight` (0.08 m, the real coping top). The deck-stranding cull now retires outside-footprint particles below `max(WallTopY, deckY + 2r)`. | `Framework/src/simulation/PbfFluid.cpp`, `Framework/include/simulation/PbfFluid.h`, `Assets/Shaders/CS_PbfCull.hlsl` |
| Anisotropic particles (R2) | New kernel `CS_PbfAnisotropy` (Yu and Turk 2013): Laplacian-smoothed centre, weighted covariance, Jacobi eigen-decomposition, stretch relative to the continuum variance 0.15 R² (measured 0.0100 vs 0.0101 m²), ratio/stretch caps, sparse neighbourhoods blend to small droplets; settled lone droplets get a negative bound and are not drawn (R9). `CS_FluidSplat` ray-casts each ellipsoid for the depth (sphere fallback when the buffer is unset), with a tight screen rectangle from the dual quadric. The thickness stays isotropic. | `Assets/Shaders/CS_PbfAnisotropy.hlsl` (new), `CS_FluidSplat.hlsl`, `compute_kernels.json` (`pbf_anisotropy`; `particle_fluid_splat` gains t2), `PbfFluid.*` (`ComputeAnisotropy`, buffer published as `BackyardWaterAnisotropy`), `ParticleSystem.h` (`anisotropy_buffer`, `screen_space.anisotropy`), `RenderGraph.cpp` (declared-buffer check), `ComputeKernelRegistry.cpp` (splat constants: P00, anisotropic flag), `Backyard8Scene_RenderGraph.json` |
| Smoothing (R4) | `CS_ParticleSmooth`: local plane from the centre's gradient; 5x5 B3 taps in 12 symmetric pairs, a pair used only when both ends are valid; range test against the plane prediction with a Gaussian range weight. | `Assets/Shaders/CS_ParticleSmooth.hlsl` |
| Shading (R3, R5–R8) | `CS_ParticleShade` rewritten: water path = geometric gap to the floor under the pool surface, chord sum for water in the air; Snell refraction through that column with the lookup pulled back until both bilinear corners lie under water; procedural sun-projected caustics on the submerged floor/walls; Beer–Lambert with in-scatter; world-space wind ripples on up-facing water, octaves faded by pixel footprint; SSR without jitter, ~1° glossy cone, exit-pixel continuation weighted by the distance still to travel, camera-facing rays to the sky; horizon-to-zenith sky; Fresnel from the macro/ripple half-normal; normalised Blinn–Phong sun glint. Constants grew 52 → 64 DWORDs (D3D12 binds them as a root CBV). | `Assets/Shaders/CS_ParticleShade.hlsl`, `compute_kernels.json`, `ComputeKernelRegistry.cpp` (`ParticleShadeConstants`, 256 bytes) |
| Config and GUI | New `screenSpace` keys: `skyHorizon`, `refractionStrength`, `glintExponent`, `rippleSlope/Wavelength/Speed/Distance`, `caustics`, `causticScale/Speed/Depth`, `anisotropy*`; `refraction` stays the splat style's path-per-metre. Debug views 5 reflection, 6 refraction (3 is now the water path). Dev GUI: sky, refraction strength, glint, "Surface detail" section. | `DayScene/Backyard8FluidJson.h`, `Backyard8Scene.cpp/.h`, `Assets/Scenes/Backyard8Fluid.json`, `Framework/src/scene/ParticleSystem.cpp` (validation) |
| Water start | `prefillLevel` −0.27 → −0.04 (the pool starts at its resting level). With the real rim the sphere drop throws water over the coping; the pool settles at −0.170 m. No prefill reaches −0.04 (prefill 0.0 still ends at −0.157). | `Assets/Scenes/Backyard8Fluid.json` |
| Regression gate | Per-start level targets: lake −0.040 ± 0.015, water −0.170 ± 0.020. Capture references re-frozen on Vulkan. | `scripts/validation/backyard8_regression.py`, `backyard8_references.json` |

## 3. Results (Vulkan, 1920×1080, screenshot camera and default camera)

- Domes, crosshatch, near-coping lines, refraction islands, reflection patchwork, dotted rows
  and the step "snowman" are gone; the pool shows its floor, walls, steps and caustics through
  clear turquoise water, with rippled reflections of the house, windows and pillars.
- The water start (frames 150–420) shows a sloshing wave with natural reflections and spray at
  the crest.
- Debug views 1 (normals), 3 (water path), 5 (reflection) and 6 (refraction) were used to
  isolate R6–R8; each is smooth after the fix.

## 4. Cost (GPU timestamps, `--profile --profileFrames 400`, Vulkan)

| Pass | Default camera | Close camera (eye 1.2 m above the deck, 11 m back) |
|---|---|---|
| Fluid Splat, plain splat mode | 0.88 ms | 3.7 ms |
| Fluid Splat, SSFR spheres (`anisotropy: false`) | 1.39 ms | — |
| Fluid Splat, SSFR anisotropic | 1.85 ms | 7.0 ms |
| Fluid Smooth (each of 6) | 0.02 ms | 0.02 ms |
| Fluid Surface Shade | 0.07 ms | 0.10 ms |

The close-camera cost is the one-thread-per-particle scatter: near particles cover up to 64 px
radius and serialise their thread (thickness plus depth loop). The tight ellipsoid rectangle did
not change it measurably; a plain read before `InterlockedMin` made it slower (8.5 ms) and was
dropped. The fix is to split large footprints across threads (tile them) or splat at half
resolution; see §6.

## 5. Verification

| Check | Result |
|---|---|
| `cmake --build build/linux --target DayScene` | builds |
| `DayScene --unattended --game-selftest` | 78 PASS, exit 0 |
| `scripts/validation/check_gl_twins.py` | PASS (54 kernels, 49 twins); twins regenerated with SPIRV-Cross 57e4efc, which reproduces untouched twins byte-for-byte |
| `scripts/validation/check_fluid_constants.py` | PASS |
| `scripts/validation/backyard8_regression.py` (Vulkan) | RESULT: PASS. References re-frozen, then an independent run: 9 captures within 0.18 % pixels, 3 px bbox, 0.48 mean levels; lake −0.0438 m, water −0.1705 m |
| glslangValidator + spirv-val | all changed kernels |

To verify the other backends on Windows (from `T850/`):

```powershell
python scripts\validation\backyard8_regression.py --api d3d12 --day-scene bin\x64\Release\DayScene.exe
python scripts\validation\backyard8_regression.py --api d3d11 --day-scene bin\x64\Release\DayScene.exe
python scripts\validation\backyard8_regression.py --api gl    --day-scene bin\x64\Release\DayScene.exe
```

The capture references are Vulkan values; expect the screen-space mean to differ slightly per
backend. A D3D11 run under Proton is not evidence (Wine's built-in compiler).

## 6. Remaining work

1. **Close-up splat cost** (§4): tile footprints above ~16 px radius into a second dispatch, or
   splat depth/thickness at half resolution with a bilateral upsample.
2. **Off-screen reflections**: the fallback is still a sky gradient plus the exit-pixel
   continuation; binding the IBL cubemap (or a probe at the pool) to the shade would replace
   both.
3. **Whitewater** at breaking crests: only isolated droplets are classified as spray. A
   per-particle velocity/curvature foam measure from `CS_PbfAnisotropy` (it already reads
   velocities) would add crest foam.
4. **Windows backends**: run the gate above; re-freeze per-backend references only if the
   differences are understood.
