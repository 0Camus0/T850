# Screen-Space Fluid Rendering in Backyard Scene 8

Status: implemented and running on Vulkan (native) and OpenGL (Windows build under Wine).
D3D11/D3D12 create and dispatch the same kernels but the screen-space result does not land;
they fall back to the splat resolve underneath, so the fluid is visible (a follow-up). Last
updated 2026-11-08.

**Backend status**

| Backend | Screen-space | Evidence |
|---|---|---|
| Vulkan (native) | working | frame 45 particle RT 46,824 px, bbox `x[186,454] y[85,290]` at 640x360; normals/depth/thickness debug views |
| OpenGL (Windows build under Wine) | working | frame 45 RT 105,507 px at 960x540 -> scaled bbox `x[186,454] y[85,291]`, matching Vulkan; frame 300 matches the Vulkan settled pool |
| D3D11 (Proton) | falls back to splat | all pipelines create and dispatch; the shade output does not reach the target, so the splat resolve underneath is visible. Not a black pool. |
| D3D12 (Proton) | falls back to splat | same as D3D11 |

This note scopes the gap between the particle renderer and

> Wladimir J. van der Laan, Simon Green, Miguel Sainz.
> **Screen Space Fluid Rendering with Curvature Flow.** NVIDIA / Rijksuniversiteit
> Groningen, 2009. `Screen_space_fluid_rendering_with_curvature_flow.pdf`.

## What the paper does

1. **Surface depth (§3.1).** Rasterise each particle as a screen-aligned sphere and keep the
   nearest depth (`z` in view space). A density threshold can remove stray/spray particles.
2. **Curvature-flow smoothing (§3.2).** Evolve the depth field `∂z/∂t = H` (mean curvature),
   with the spatial derivatives forced to zero across silhouettes and screen edges. This
   removes the blobby/jelly look of individual splats without a bilateral filter.
3. **Thickness (§3.3).** Additively accumulate the amount of fluid along each view ray. It
   drives the transparency and the colour attenuation.
4. **Noise / foam (§3.4).** Add a Perlin-noise layer that is advected with the particles, and
   boost it where the velocity changed sharply.
5. **Shading (§3.5).** Reconstruct view-space normals from the depth field, shade with the
   Fresnel equation, refract the background by `β = T·γ·n`, attenuate by `e^{-T}`, add a
   Phong specular highlight and environment reflection. Optional half-resolution processing
   and interpolation close the quality/performance gap.

## What scene 8 now implements

| Paper step | Implementation | File |
|---|---|---|
| Surface depth | `CS_ParticleDepth` reuses the splat stamp's projected table and 8x8 tile mask, evaluates the front sphere intersection per pixel, and writes **surface depth**, **thickness**, **kernel weight** and **coverage** to `FluidParticlesDepthA` | `Assets/Shaders/CS_ParticleDepth.hlsl` |
| Thickness | Sum of the per-particle sphere chords along the ray, weighted by the splat kernel and the scene-depth occlusion band | same |
| Curvature-flow smoothing | `CS_ParticleSmooth` runs four ping-pong passes (`A→B→A→B→A`). Each pass is a tolerance-guarded, radially weighted low-pass of the depth field. `smoothingIterations` (0–4) selects how many passes do work; the others copy. | `Assets/Shaders/CS_ParticleSmooth.hlsl` |
| Normals | Finite differences of the smoothed depth with the paper's "smallest absolute difference" silhouette rule, and a configurable pixel stencil (`normalStep`). Normal = `normalize(-Fx·dz/dx, -Fy·dz/dy, -z)` in view space. | `Assets/Shaders/CS_ParticleShade.hlsl` |
| Fresnel / refraction / absorption | Fresnel over the view vector, screen-space refraction offset of the scene colour, Beer-Lambert `exp(-T·σ)` with a per-channel absorption tint, screen-space reflection blended with a sky colour for rays leaving the screen, Blinn-Phong sun highlight | `Assets/Shaders/CS_ParticleShade.hlsl` |
| Noise / foam | A `foam` term brightens thin fluid; no advected Perlin layer yet (see gaps) | same |
| Runtime switch | `ParticleSystemDesc::render_mode` is read by the shade constants builder every frame. The shade pass early-outs in Splat mode, leaving the splat target intact. The Dev GUI combo switches live; `--fluid-render splat|screen-space` sets the startup mode. | `ParticleSystem.h`, `ComputeKernelRegistry.cpp`, render graph |

Render-graph chain (all in `Assets/Scenes/Backyard8Scene_RenderGraph.json`):

```
Fluid Particles (stamp, resolve)     -> FluidParticles (colour, coverage)
Fluid Surface Depth                  -> FluidParticlesDepthA (depth, thickness, coverage)
Fluid Smooth 1..4                    -> A/B ping-pong, final in A
Fluid Surface Shade                  -> FluidParticles (screen-space replaces splat)
Fluid Particle Composite             -> over the deferred scene
```

The graph always contains the full chain. `render_mode` is a runtime constant, so switching
does not rebuild pipelines or reload the graph. In Splat mode the depth pass writes an empty
surface, the smoothing passes copy it and the shade pass takes its `Mode == 0` early-out; the
splat resolve is always active so a backend whose screen-space chain produces no depth still
shows the fluid. The shade pass deliberately does not write zero on empty coverage, which is
what makes that fallback work. Skipping the depth/smoothing dispatches entirely in Splat mode
is a future graph-stage optimisation.

## Deliberate deviations from the paper

- **Smoothing kernel.** The implementation uses a tolerance-guarded radial box filter rather
  than an explicit integration of `∂z/∂t = H`. It is a stable, separable-cost approximation of
  the same low-pass flow and has the same silhouette boundary condition. The exact PDE is
  listed as the next quality step.
- **Surface depth.** The depth pass writes the **kernel-weighted average** of the particle
  sphere fronts, not the strict minimum. The minimum is the particle-lattice envelope and is
  too bumpy for a one-pixel normal stencil; the weighted average is the same surface
  low-pass-filtered by the splat kernel. The minimum is still used for the scene-depth
  occlusion test.
- **Reflection.** Screen-space reflection is a scene-colour lookup blended with a configurable
  sky colour; there is no cubemap/SSR ray march.
- **Thickness** is a kernel-weighted chord sum, not an exact volume integral.
- **Noise/foam** is a grey brightening term, not the paper's advected Perlin octaves.
- No half-resolution processing or spray separation yet.

## How far from the paper (gap list, in priority order)

1. **Exact curvature flow.** Replace the box filter with the paper's finite-difference
   mean-curvature step (Eqs. 1–8), including the `Cx/Cy` focal-length scaling. The current
   filter already removes the lattice; the PDE would preserve sharper silhouette curvature.
2. **Half-resolution depth/smoothing.** Run depth + smoothing at half resolution and upsample
   the depth/normal for shading. Roughly 4x cheaper at 1440p/4K.
3. **Advected Perlin foam.** Add a per-particle noise octave and velocity-change marking
   (Eqs. 10–12), then perturb normals and add a grey foam term.
4. **Environment reflection.** Sample the scene's cubemap along the reflected view vector
   instead of a flat sky colour.
5. **Spray separation.** Threshold low-density particles and render them in the splat path
   while the coherent body uses screen-space.
6. **Exact thickness.** Integrate density along the view ray (the paper's Eq. 9) or use a
   separable thickness pass.

## Tuning

Everything is in `Assets/Scenes/Backyard8Fluid.json` under `particles` and `screenSpace`:

```json
"particles": { "renderMode": 1, "style": 0, "radius": 0.13, "opacity": 0.82, ... },
"screenSpace": {
  "smoothingIterations": 4, "smoothingStrength": 0.85, "smoothingRadius": 10.0,
  "depthTolerance": 0.35, "thicknessScale": 1.0,
  "absorption": [3.0, 1.0, 0.5], "reflection": [0.45, 0.62, 0.85],
  "refraction": 0.02, "specular": 1.0, "fresnelPower": 5.0,
  "normalScale": 0.7, "normalStep": 4.0, "foam": 0.0, "debugView": 0
}
```

`debugView` renders the intermediate fields: 1 normals, 2 surface depth, 3 thickness,
4 coverage. The Dev GUI exposes the same values, plus the splat look/style and solver knobs,
and writes them back into the scene config object so `Apply & re-seed` uses exactly what is
on screen.
