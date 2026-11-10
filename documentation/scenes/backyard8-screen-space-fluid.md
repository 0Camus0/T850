# Screen-Space Fluid Rendering in Backyard Scene 8

Status: 2026-11-09 rendering fixes (edge-aware a-trous smoothing, Snell refraction with occlusion
rejection, screen-space reflection, coverage-only alpha, spray, and a particle renderer that
blends the particles into water, with lit spheres as a debug style) are verified on **Vulkan
only**. D3D12, D3D11 and OpenGL must be re-verified
with [backyard8-fluid-render-d3d-port.md](backyard8-fluid-render-d3d-port.md), which also lists
what was wrong and every changed file. The 2026-11-08 cross-backend results below predate the fixes.

This note scopes the gap between the particle renderer and

> Wladimir J. van der Laan, Simon Green, Miguel Sainz.
> **Screen Space Fluid Rendering with Curvature Flow.** NVIDIA / Rijksuniversiteit
> Groningen, 2009. `Screen_space_fluid_rendering_with_curvature_flow.pdf`.

The implementation record is
[diagnosis-3-implementation.md](diagnosis-3-implementation.md); the plan is
[diagnosis-3.md](diagnosis-3.md).

## Current pipeline

```
PBF positions ─► CS_ParticleSplat (stamp) ─► __particle_projected_BackyardWater
                                                   │
                       CS_FluidClear ──────────────┤ (depth bits / thickness bits)
                       CS_FluidSplat (scatter) ────┤  one thread per particle:
                                                   │  InterlockedMin(front depth)
                                                   │  InterlockedAdd(thickness chord)
                       CS_FluidResolve ────────────┴─► FluidDepthA (F32)
                                                        FluidThickness (RGBA16F)
                       CS_ParticleSmooth x6 (edge-aware a-trous, F32 ping-pong)
                                                   │
                       CS_ParticleShade (compute) ─┴─► FluidParticles (RGBA16F)
                                                   │
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
| Surface depth | `CS_FluidSplat` scatters the minimum sphere front with `InterlockedMin` on the float bits. One thread per particle, so cost is particles × disc area, not screen coverage × particle count. |
| Depth precision | `FluidDepthA/B` are F32 (R32F). RGBA16F had 10 mantissa bits, which terraced normals at 4–16 m. |
| Thickness | Chord sum, unweighted, divided by the lattice packing factor (4.19), stored as 1/1024 m fixed point, blurred 5x5 in the resolve. `a` is a silhouette anti-alias coverage, not opacity. |
| Smoothing | Edge-aware a-trous: 5x5 B3 taps at spacing 1, 2, 4, 8, 16, then 1. Each pixel stops once the spacing exceeds half its projected `smoothingWorldRadius` (0.3 m), so the filter has a fixed world size; neighbours more than `depthTolerance` (0.3 m) nearer or farther are another layer. A depth-blind Gaussian merged layers (dark seams) and separable passes left vertical streaks. |
| Normals | View-space positions from the smoothed depth; central differences inside the surface, one-sided across a jump of more than two particle radii; cross product. |
| Shading | Paper Eq. 13: Snell refraction (path `refraction` × thickness, ≤ 2 m) with samples of geometry in front of the water rejected, Beer–Lambert absorption, screen-space reflection traced against `GBuffer:DEPTH` with the on-screen sky as the miss colour, Schlick Fresnel, sun specular, spray whitewater for isolated droplets. |
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
2. **Half-resolution depth/smoothing** with bilateral upsample. This is the main performance
   lever and the prerequisite for the paper's iteration counts. The graph currently has no
   `$window_half` size reference.
3. **Full-screen graphics shade with environment reflection.** The compute shade cannot bind
   the IBL cubemap; reflection is a screen-space trace with the on-screen sky as the miss
   colour. Moving to `FS_Quad` with `bind_environment_map` would add off-screen reflections.
4. **Perlin noise/foam** (Eqs. 10–12) and **spray classification** by density. Spray is
   currently detected in screen space (an isolated droplet's surroundings are empty) and drawn
   as whitewater; the solver density buffer is not published to the renderer yet.
5. **Exact thickness** (Eq. 9) and optional density-based spray separation.

## Tuning

Everything is in `Assets/Scenes/Backyard8Fluid.json`: `particles` (render mode, style, radius,
opacity, colours, draw count) and `screenSpace` (passes, strength, world filter radius and its
pixel cap, layer threshold, depth radius, packing factor, thickness, absorption, fluid colour,
sky/reflection colour, refraction path, reflection range, specular, Fresnel, normal
scale/stencil, spray whitewater (`foam`), splat fallback, debug view). The Dev GUI exposes the
same values and writes them back into the in-memory config.

`debugView`: 1 normals, 2 surface depth, 3 thickness, 4 coverage.

`particles.splatFallback` (Dev GUI “keep splat underneath”) restores the old splat resolve as a
debug fallback. It is off by default; turn it on for Proton D3D11 or while investigating a
backend whose screen-space result is missing.
