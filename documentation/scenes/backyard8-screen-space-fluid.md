# Screen-Space Fluid Rendering in Backyard Scene 8

Status: 2026-11-10 realism fixes (solver rim at the real coping, Yu–Turk anisotropic
ellipsoids, plane-aware smoothing, and a rewritten shade with the water column, caustics,
ripples, glossy SSR with off-frame continuation) are verified on **Vulkan only**; see
[backyard8-ssfr-realism.md](backyard8-ssfr-realism.md) for the root causes, changed files, cost
and the Windows verification commands. The 2026-11-10 whitewater pass (Ihmsen 2012 foam and
aeration, secondary spray/foam/bubble pool, wet deck, environment-cube reflections, G-buffer
sun and caustics under water, half-resolution splat) is recorded in
[backyard8-ssfr-whitewater.md](backyard8-ssfr-whitewater.md) (draft, cost and regression
numbers TBD pending WO-4). The 2026-11-09 fixes are recorded in
[backyard8-fluid-render-d3d-port.md](backyard8-fluid-render-d3d-port.md). The 2026-11-08
cross-backend results below predate all of these.

This note scopes the gap between the particle renderer and

> Wladimir J. van der Laan, Simon Green, Miguel Sainz.
> **Screen Space Fluid Rendering with Curvature Flow.** NVIDIA / Rijksuniversiteit
> Groningen, 2009. `Screen_space_fluid_rendering_with_curvature_flow.pdf`.

The implementation record is
[diagnosis-3-implementation.md](diagnosis-3-implementation.md); the plan is
[diagnosis-3.md](diagnosis-3.md).

## Current pipeline

```
 PBF positions ─► CS_PbfAnisotropy (after the solver step) ─► BackyardWaterAnisotropy
               │      (smoothed centre + ellipsoid quadric + spray flag per particle)
               └► CS_ParticleSplat (stamp) ─► __particle_projected_BackyardWater
                                                    │
   ┌─ solver whitewater kernels (2026-11-10, see backyard8-ssfr-whitewater.md) ─────────────┐
   │  CS_PbfWhitewater ─► BackyardWaterWhitewater (per-particle foam/air/agitation),        │
   │                       BackyardWaterDiffuse (secondary spray/foam/bubble pool),         │
   │                       BackyardWaterWetness (deck wetness map)                          │
   │  CS_PbfDiffuse ─────► BackyardWaterDiffuse (classify by fluid neighbours, move, land)  │
   │  CS_PbfFields mode 0 (whitewater-dry) ─► wetness drying                                │
   │  CS_PbfFields mode 1 (whitewater-caustics) ─► BackyardWaterCaustics (focus field)      │
   └─────────────────────────────────────────────────────────────────────────────────────────┘
                                                    │
                        CS_FluidClear ──────────────┤ (depth bits / thickness bits / aux bits,
                        CS_FluidSplat (scatter) ────┤  all at half resolution, $window_half)
                                                    │  one thread per particle:
                                                    │  InterlockedMin(ellipsoid front depth)
                                                    │  InterlockedAdd(thickness chord, spheres)
                                                    │  aux channels: foam / aeration /
                                                    │  agitation / spray (from the solver)
                        CS_FluidResolve ────────────┴─► FluidDepthA (F32, half res)
                                                         FluidThickness (RGBA16F, half res)
                        CS_ParticleSmooth x6 (plane-aware a-trous, F32 ping-pong, half res)
                                                    │
                        CS_FluidWhitewater ─────────► __whitewater_bits (spray/foam/bubble/
                                                    │  sparkle channels, full res)
                        CS_ParticleShade (compute) ─┴─► FluidParticles (RGBA16F, full res)
                                                    │  depth-aware upsample of the half-res
                                                    │  depth; reads @environment_map (cube),
                                                    │  GBuffer:COLOR0/1, ShadowAccum,
                                                    │  BackyardWaterCaustics,
                                                    │  BackyardWaterWetness
                        PARTICLE_COMPOSITE ─────────┴─► over Deferred
```

The particle renderer (render mode splat, style water or spheres) reuses the stamp, clear and
scatter: `CS_FluidSplat` with `ScreenSpace = 0` keeps the nearest sphere per pixel as a packed
(depth, index) key and still sums the thickness. `CS_FluidSphereShade` then draws either
**water** (style 0, default): the summed thickness, smoothed, read as a height field for the
normal, with the same refraction, absorption, Fresnel sky reflection and sun highlight as the
screen-space shade, and edges that fade with thickness; or **spheres** (style 3, debug): the
nearest particle lit as an opaque sphere. The water style has no front-surface
reconstruction, so it reflects only the sky and its normals are softer; screen space remains
the final look.

Key properties:

| Concern | Implementation |
|---|---|
| Surface depth | `CS_FluidSplat` scatters the minimum front with `InterlockedMin` on the float bits. With the anisotropy buffer each particle is an ellipsoid ray-cast inside the tight screen rectangle of its dual quadric (flat discs on a free surface, films in a sheet); settled lone droplets (negative bound) are skipped; an unset buffer (zero bound) falls back to spheres. One thread per particle, so cost is particles × footprint, not screen coverage × particle count. |
| Depth precision | `FluidDepthA/B` are F32 (R32F). RGBA16F had 10 mantissa bits, which terraced normals at 4–16 m. |
| Thickness | Chord sum of isotropic spheres, unweighted, divided by the lattice packing factor (4.19), stored as 1/1024 m fixed point, blurred 5x5 in the resolve. `a` is a silhouette anti-alias coverage, not opacity. |
| Smoothing | Plane-aware a-trous: 5x5 B3 taps at spacing 1, 2, 4, 8, 16, then 1, taken in symmetric pairs (a pair counts only when both ends are valid, so occlusion boundaries are not pulled toward the camera); the range test measures each tap against the local plane, not the centre depth, so grazing surfaces still smooth. Each pixel stops once the spacing exceeds half its projected `smoothingWorldRadius` (0.3 m); taps more than `depthTolerance` (0.3 m) off the plane are another layer. |
| Normals | View-space positions from the smoothed depth; central differences inside the surface, one-sided across a jump of more than two particle radii; cross product; world-space wind ripples added on up-facing water, octaves faded by the pixel footprint. |
| Shading | Paper Eq. 13 with physical terms: water path = geometric gap to the floor under the pool surface (chord sum for water in the air); Snell refraction through that column, the lookup pulled back until both bilinear corners lie under water; sun-projected caustics on the submerged floor and walls; Beer–Lambert with in-scattered colour; SSR against `GBuffer:DEPTH` over a ~1° glossy cone, rays that leave the frame continue the object at the exit pixel, camera-facing rays and misses take a horizon-to-zenith sky; Schlick Fresnel from the macro/ripple half-normal; normalised Blinn–Phong sun glint; spray whitewater for isolated droplets. |
| Composite | Alpha = coverage (lowered only for spray). The splat resolve is disabled in screen-space mode, so the background is not blended in twice. |
| Runtime switch | `ParticleSystemDesc::render_mode`; `--fluid-render splat\|screen-space`; the Dev GUI renderer combo. The scene enables/disables the graph passes without rebuilding pipelines or the graph. |

## Verified results (frame 24, 960×540, 2026-11-08, before the 2026-11-09 fixes)

| Backend | Particle target | Mean RGB |
|---|---|---|
| Vulkan (native) | 122,702 px, bbox `x[264,695] y[0,342]` | (11.1, 29.7, 32.8) |
| D3D12 (Proton, DXC/vkd3d) | 122,702 px, same bbox | (11.1, 29.7, 32.8) |
| OpenGL (Wine) | 122,702 px, same bbox | (11.1, 29.6, 32.6) |
| D3D11 (Proton, Wine compiler) | 122,702 px, same bbox | (59.6, 60.4, 60.4) — geometry correct, shading wrong (F2) |

The normal quadrant test at frame 24 gives left `n.x = −0.54` / 100%, right `+0.53` / 100%,
bottom `n.y = −0.70` / 100% correct sign. Vulkan and OpenGL frame 300 both show a continuous
blue pool with a specular waterline; the old dark band and octagonal terraces are gone. (GL
frame 300: 85,115 px, bbox `x[0,959] y[269,479]`, mean (12.1, 22.8, 25.5).)

## What the paper still asks for

1. **Exact curvature flow.** The exact mean-curvature update was implemented and tested; at 8
   and 40 full-resolution iterations it reduced but did not remove the cubic-lattice facets.
   The shipped filter is the edge-aware a-trous pass chain. Exact flow needs the paper's 40–100
   iterations, ideally at half resolution.
2. **Half-resolution depth/smoothing** with bilateral upsample. **Addressed on 2026-11-10**
   (whitewater pass): the graph now has a `$window_half` size reference; `FluidDepthA/B` and
   `FluidThickness` are half resolution, the splat tests the farthest full-resolution depth of
   each half pixel, and the shade upsamples with a depth-aware bilinear filter. The smoothing
   is still the a-trous chain, not the paper's exact curvature flow (item 1).
3. **Full-screen graphics shade with environment reflection.** **Addressed on 2026-11-10**:
   the compute shade now binds the IBL cubemap (`@environment_map`, a new cube binding for
   compute passes); rays the screen trace cannot resolve sample the cube instead of the sky
   gradient. The shade is still a compute pass, not `FS_Quad`.
4. **Perlin noise/foam** (Eqs. 10–12) and **spray classification** by density. **Partly
   addressed on 2026-11-10**: foam is now simulated per particle (Ihmsen 2012 potentials,
   carried foam/aeration that decay, foam lace texture, secondary spray/foam/bubble pool) and
   spray is classified by the solver (per-particle spray flag from the anisotropy pass plus the
   secondary pool's fluid-neighbour count), not by the screen-ring heuristic. What remains:
   the paper's Perlin-noise foam (Eqs. 10–12) is not implemented; the foam lace is a procedural
   texture instead.
5. **Exact thickness** (Eq. 9) and optional density-based spray separation. Still open.

## Tuning

Everything is in `Assets/Scenes/Backyard8Fluid.json`: `particles` (render mode, style, radius,
opacity, colours, draw count, anisotropy buffer) and `screenSpace` (passes, strength, world
filter radius and its pixel cap, layer threshold, depth radius, packing factor, thickness,
absorption, fluid colour, zenith (`reflection`) and horizon (`skyHorizon`) sky, splat refraction
path (`refraction`), SSFR `refractionStrength`, reflection range, specular, `glintExponent`,
Fresnel, normal scale/stencil, spray whitewater (`foam`), ripples (`rippleSlope`,
`rippleWavelength`, `rippleSpeed`, `rippleDistance`), caustics (`caustics`, `causticScale`,
`causticSpeed`, `causticDepth`), anisotropy (`anisotropy`, `anisotropySmoothing`,
`anisotropyMaxRatio`, `anisotropyRadius`, `anisotropySprayRadius`, `anisotropyNeighbours`,
`anisotropySprayNeighbours`, `anisotropyMaxStretch`), splat fallback, debug view). The Dev GUI
exposes the same values and writes them back into the in-memory config.

The 2026-11-10 whitewater pass added 16 more `screenSpace` keys (scene lighting:
`envIntensity`, `sunAttenuation`, `causticFocus`, `contactLine`, `backlight`, `scatterSun`,
`scatterSky`, `scatterAnisotropy`, `rippleCalm`, `rippleAgitation`; whitewater: `foamStrength`,
`foamTextureScale`, `aeration`, `bubbleStrength`, `sprayStrength`, `wetness`) and a new
top-level `whitewater` section mirroring the solver's `PbfWhitewaterSettings` (potentials,
gains/lifetimes, secondary particle pool, drying time, wetness map, caustic gain). The JSON
values equal the C++ defaults. Full key-by-key list and the Dev GUI sections ("Scene lighting",
"Whitewater"): [backyard8-ssfr-whitewater.md](backyard8-ssfr-whitewater.md) §3.

`debugView`: 1 normals, 2 surface depth, 3 water path, 4 coverage, 5 reflection, 6 refraction,
7 whitewater (r foam, g aeration, b spray), 8 sun light under water, 9 wetness.

`particles.splatFallback` (Dev GUI “keep splat underneath”) restores the old splat resolve as a
debug fallback. It is off by default; turn it on for Proton D3D11 or while investigating a
backend whose screen-space result is missing.
